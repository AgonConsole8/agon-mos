// System variables support
//

#include <stdlib.h>
#include <stdio.h>

#include "mos_sysvars.h"

t_mosSystemVariable * mosSystemVariables = NULL;

// Get a system variable
// Parameters:
// - pattern: The pattern to search for
// - var: Pointer to the variable to return
// if on entry var is NULL, the search will start from the beginning of the list
// otherwise it will return the first variable after the one pointed to by var
// Returns:
// - var will be updated to point to the variable found, or the variable before the first match
// - 0 if a match was found
// - 1 if no match was found
// - pattern match score if a partial match was found
// 
int getSystemVariable(char *pattern, t_mosSystemVariable **var) {
	t_mosSystemVariable *current;
	int matchResult = -1;

	if (*var == NULL) {
		current = mosSystemVariables;
	} else {
		current = (t_mosSystemVariable *)(*var)->next;
	}
	*var = NULL;

	while (current != NULL) {
		// We match up to the first space in pattern (or end of pattern if no spaces), case insensitive
		matchResult = pmatch(pattern, current->label, MATCH_CASE_INSENSITIVE | MATCH_UP_TO_SPACE);
		if (matchResult <= 0) {
			*var = current;
		}
		if (matchResult == 0) {
			break;
		}
		if (matchResult > 0) {
			// Since the list is sorted, we can stop searching
			break;
		}
		current = (t_mosSystemVariable *)current->next;
	}

	if (*var != NULL) {
		return matchResult;
	} else {
		return -1; // Token/pattern not found
	}
}

t_mosSystemVariable * createSystemVariable(char * label, MOSVARTYPE type, void * value) {
	t_mosSystemVariable * newVar = umm_malloc(sizeof(t_mosSystemVariable));
	char * newLabel = mos_strdup(label);
	if (newVar == NULL || newLabel == NULL) {
		if (newVar) umm_free(newVar);
		if (newLabel) umm_free(newLabel);
		return NULL;
	}
	newVar->label = newLabel;
	newVar->value = value;
	newVar->type = type;

	newVar->next = NULL;

	return newVar;
}

void insertSystemVariable(t_mosSystemVariable * var, t_mosSystemVariable * before) {
	if (before == NULL) {
		var->next = mosSystemVariables;
		mosSystemVariables = var;
	} else {
		var->next = before->next;
		before->next = var;
	}
}

void updateSystemVariable(t_mosSystemVariable * var, MOSVARTYPE type, void * value) {
	if (var->type == MOS_VAR_MACRO || var->type == MOS_VAR_STRING) {
		umm_free(var->value);
	}

	if (var->type == MOS_VAR_CODE) {
		// Call setter function, if we have a write function
		if (((t_mosCodeSystemVariable *)var->value)->write != NULL) {
			((t_mosCodeSystemVariable *)var->value)->write(value);
		}
		return;
	}

	var->type = type;
	var->value = value;
}

// delete system variable object
void removeSystemVariable(t_mosSystemVariable * var) {
	t_mosSystemVariable * parent = findParentSystemVariable(var);
	if (parent == NULL) {
		// we are deleting the first item in the list
		mosSystemVariables = (t_mosSystemVariable *)var->next;
	} else {
		parent->next = (t_mosSystemVariable *)var->next;
	}
	umm_free(var->label);
	if (var->type == MOS_VAR_MACRO || var->type == MOS_VAR_STRING) {
		umm_free(var->value);
	}
	umm_free(var);
}

// find parent system variable object (used for deleting)
t_mosSystemVariable * findParentSystemVariable(t_mosSystemVariable * var) {
	t_mosSystemVariable * current = mosSystemVariables;
	t_mosSystemVariable * parent = NULL;

	while (current != NULL) {
		if (current == var) {
			return parent;
		}
		parent = current;
		current = (t_mosSystemVariable *)current->next;
	}

	return NULL;
}


t_mosTransInfo * gsInit(void * source, void * parent) {
	// TODO store pointer to most recent transInfo object
	// so we can delete all objects in the chain if a new init is called
	// as an aborted read sequence may leave us with a chain of objects

	// Set up a t_mosTransInfo object
	t_mosTransInfo * transInfo = umm_malloc(sizeof(t_mosTransInfo));
	transInfo->source = source;
	transInfo->parent = parent;
	transInfo->type = MOS_VAR_MACRO;

	return transInfo;
}

int gsRead(t_mosTransInfo ** transInfo, char * read) {
	// read next char from our source,
	// which should either be of type MOS_VAR_STRING, MOS_VAR_NUMBER, or MOS_VAR_MACRO
	// our info may have metadata to help us read the next char, such as MOS_VAR_NUMBER
	// on reaching end of current item, we need to dispose of it and move back to the parent
	t_mosTransInfo * current = *transInfo;
	int result = FR_OK;

	if (current == NULL) {
		// We have reached the end of the chain
		return FR_OK;
	}

	*read = '\0';

	// Do transformation based on type, if we need to
	switch (current->type) {
		case MOS_VAR_LITERAL:
		case MOS_VAR_STRING:
		case MOS_VAR_CODE:
		{
			*read = *current->source++;
			if (*read == '\0') {
				// end of the string so dispose of this transInfo object
				// and move back to the parent
				if (current->type == MOS_VAR_CODE) {
					// free cached string, pointer is stored in extraData
					umm_free(current->extraData);
				}
				*transInfo = current->parent;
				umm_free(current);
				// return next read from parent
				result = gsRead(transInfo, read);
			}
			break;
		}

		case MOS_VAR_NUMBER: {
			// our transInfo object will contain the number as "source"
			// and extraData is the current divisor
			// we modify the source to be the remainder each time around

			// If our divisor is 0, then we have finished
			if ((int)current->extraData == 0) {
				*read = '\0';
				*transInfo = current->parent;
				umm_free(current);
				// return next read from parent
				result = gsRead(transInfo, read);
				break;
			}

			*read = '0' + ((int)current->source / (int)current->extraData);
			current->source = (void *)((int)current->source % (int)current->extraData);
			current->extraData = (void *)((int)current->extraData / 10);
			break;
		}

		case MOS_VAR_MACRO: {
			*read = *current->source++;
			switch (*read) {
				case '\0': {
					// end of the macro - move back to the parent
					*transInfo = current->parent; 
					umm_free(current);
					result = gsRead(transInfo, read);
					break;
				}

				case '|': {
					// interpret pipe-escaped characters
					if (*current->source == '\0') {
						// no more characters, so this is an error
						*read = '\0';
						return MOS_BAD_STRING;
					} else if (*current->source == '?') {
						// prints character 127
						*read = 0x7F;
						current->source++;
					} else if (*current->source == '!') {
						// prints next character with top bit set
						current->source++;
						if (*current->source == '\0') {
							// no more characters, so this is an error
							*read = '\0';
							return MOS_BAD_STRING;
						}
						*read = *current->source | 0x80;
						current->source++;
					} else if (*current->source >= 0x40 && *current->source < 0x7F) {
						// characters from &40-7F (letters and some punctuation)
						// are printed as just their bottom 5 bits
						// (which Acorn documents as CTRL( ASCII(uppercase(char) – 64))
						*read = *current->source & 0x1F;
						current->source++;
					} else {
						// all other characters are passed thru
						*read = *current->source;
						current->source++;
					}
					break;
				}

				case '<': {
					// possibly a number or variable
					// so search for an end tag
					char *end = current->source;
					if (*end == ' ') {
						// leading space means this isn't a variable or number
						// so we skip on, letting the `<` drop thru
						break;
					}
					while (*end && *end != '>') {
						end++;
					}
					// if there isn't an end-tag, or we have a `<>` we do nothing, and let the `<` drop thru
					if (*end == '>' && end > current->source) {
						int number = 0;
						if (extractNumber(current->source, end, &number) != FR_OK) {
							// value up to end is not a number
							// we therefore should interpret it as a variable
							t_mosSystemVariable * var = NULL;
							*end = '\0';
							if (getSystemVariable(current->source, &var) == 0) {
								// variable found
								// Set up a new transInfo object
								t_mosTransInfo * newTransInfo = umm_malloc(sizeof(t_mosTransInfo));
								*end = '>';
								if (newTransInfo == NULL) {
									result = FR_INT_ERR;
								} else {
									newTransInfo->source = var->value;
									newTransInfo->parent = current;
									newTransInfo->type = var->type;
									*transInfo = newTransInfo;
									*end = '>';
									switch (var->type) {
										case MOS_VAR_NUMBER: {
											// extraData will track the highest power of 10 divisor
											newTransInfo->extraData = (void *)(int)1;	// Assume 1 digit
											if ((int)var->value < 0) {
												// Negative number, so we need to output a `-` sign
												newTransInfo->source = (char *)(-(int)var->value);
												*read = '-';
											}
											// Work out how many digits we have
											while (((int)newTransInfo->extraData * 10) <= (int)newTransInfo->source) {
												newTransInfo->extraData = (void *)((int)newTransInfo->extraData * 10);
											}
											*transInfo = newTransInfo;
											if ((int)var->value >= 0) {
												// Positive number, so we need to output the first digit
												result = gsRead(transInfo, read);
											}
											break;
										}
										case MOS_VAR_CODE: {
											// Cache a copy of our expanded variable
											char * newValue = expandVariable(var);
											if (!newValue) {
												*read = '\0';
												return FR_INT_ERR;
											}
											newTransInfo->source = newValue;
											// extraData will point to our cached value for later release
											newTransInfo->extraData = newValue;
											*transInfo = newTransInfo;
											// get first char from code block
											result = gsRead(transInfo, read);
											break;
										}
										default:
											result = gsRead(transInfo, read);
									}
								}
							} else {
								// variable was not found, so we need to move on to next char
								*end = '>';
								current->source = end + 1;
								return gsRead(transInfo, read);
							}
						} else {
							*read = number & 0xFF;
						}
						*end = '>';
						current->source = end + 1;
					}
					break;
				}
			}
		}
		case MOS_VAR_EXPANDED: {
			// This is used for variable lookup/evaluation???
		}
	}
	return result;
}

// Transform a source string into a destination string
// using the GSTrans rules
// Parameters:
// - source: The source string
// - dest: The destination string buffer (or NULL to just calculate the length)
// - destLen: The length of the destination buffer
// - read: Pointer to an integer to store the number of characters read from source
// Returns:
// - FR_OK if successful
// - FR_INVALID_PARAMETER if source is NULL
// - FR_INT_ERR if an internal error occurred
// - FR_BAD_STRING if a bad string was encountered
//
int gsTrans(char * source, char * dest, int destLen, int * read) {
	int remaining = destLen;
	char c;
	int result;
	t_mosTransInfo * transInfo = gsInit(source, NULL);
	// belt and braces - ensure we definitely don't write to NULL
	if (dest == NULL) {
		remaining = 0;
	}
	*read = 0;

	if (source == NULL || source == dest) {
		return FR_INVALID_PARAMETER;
	}

	if (transInfo == NULL) {
		return FR_INT_ERR;
	}

	while (transInfo != NULL) {
		result = gsRead(&transInfo, &c);
		if (result != FR_OK) {
			return result;
		}
		if (transInfo == NULL) {
			break;
		}
		*read += 1;
		if (remaining > 0) {
			*dest++ = c;
			remaining--;
		}
	}

	return FR_OK;
}

int extractNumber(char * source, char * end, int * number) {
	int base = 10;
	char *endptr = NULL;
	char *start = source;
	char lastChar = '\0';
	if (end == NULL) {
		end = source + strlen(source);
	} else {
		lastChar = *end;
		*end = '\0';
	}
	// number can be decimal, &hex, or base_number
	if (*source == '&') {
		base = 16;
		source++;
	} else {
		char *underscore = strchr(source, '_');
		if (underscore != NULL && underscore > source) {
			char *baseEnd;
			*underscore = '\0';
			base = strtol(source, &baseEnd, 10);
			if (baseEnd != underscore) {
				// we didn't use all chars before underscore, so invalid base
				base = -1;
			}
			// Move source pointer to the number part
			source = underscore + 1;
			*underscore = '_';
		}
	}

	if (base > 1 && base <= 36) {
		*number = strtol(source, &endptr, base);
	}
	if (lastChar != '\0') {
		*end = lastChar;
	}

	if (endptr != end) {
		// we didn't consume whole string, so (for now) it's not considered a valid number
		return FR_INVALID_PARAMETER;
	}

	return FR_OK;
}

// Expand a macro string
// Parameters:
// - source: The source string
// Returns:
// - The expanded string
// - NULL if an error occurred
// NB: The caller is responsible for freeing the returned string
//
char * expandMacro(char * source) {
	char * dest;
	int read;
	int result;

	result = gsTrans(source, NULL, 0, &read);
	if (result != FR_OK) {
		return NULL;
	}
	dest = umm_malloc(read + 1);
	if (dest == NULL) {
		return NULL;
	}
	result = gsTrans(source, dest, read, &read);
	if (result != FR_OK) {
		umm_free(dest);
		return NULL;
	}
	dest[read] = '\0';
	return dest;
}

// Expand a variable
// Parameters:
// - var: The variable to expand
// Returns:
// - The expanded string
// - NULL if an error occurred
// NB: The caller is responsible for freeing the returned string
//
char * expandVariable(t_mosSystemVariable * var) {
	if (var->type == MOS_VAR_MACRO) {
		return expandMacro(var->value);
	}
	if (var->type == MOS_VAR_STRING) {
		return mos_strdup(var->value);
	}
	if (var->type == MOS_VAR_NUMBER) {
		// Work out how many digits we have
		char * value;
		int number = (int)var->value;
		int digits = 1;
		int divisor = number < 0 ? -1 : 1;
		while (divisor * 10 <= number) {
			divisor *= 10;
			digits++;
		}
		if (number < 0) {
			digits++;
		}
		value = umm_malloc(digits + 1);
		if (value == NULL) {
			return NULL;
		}
		sprintf(value, "%d", number);
		return value;
	}
	if (var->type == MOS_VAR_CODE) {
		int len = 0;
		char * newValue = NULL;
		int result = FR_OK;
		// get length of our code read result
		result = ((t_mosCodeSystemVariable *)var->value)->read(NULL, &len);
		if (result != FR_OK) {
			return NULL;
		}
		newValue = umm_malloc(len);
		if (newValue == NULL) {
			return NULL;
		}
		// get result value
		result = ((t_mosCodeSystemVariable *)var->value)->read(newValue, &len);
		if (result != FR_OK) {
			umm_free(newValue);
			return NULL;
		}
		return newValue;
	}
	return NULL;
}		

t_mosEvalResult * evaluateExpression(char * source) {
	t_mosSystemVariable * var = NULL;
	int number = 0;
	t_mosEvalResult * result = umm_malloc(sizeof(t_mosEvalResult));
	if (result == NULL) {
		return NULL;
	}

	result->result = NULL;
	result->status = FR_OK;

	// Preliminary evaluate
	// this will not attempt to evaluate the expression
	// instead it will first attempt to interpret source as a number
	// and if that fails, it will attempt to interpret it as a variable
	// expanding any macros to strings
	// it will return the type of the result, and a copy of the result itself

	// First try to interpret source as a number
	if (extractNumber(source, NULL, &number) == FR_OK) {
		result->type = MOS_VAR_NUMBER;
		result->result = (void *)number;
		return result;
	}

	// Next try to interpret source as a variable
	if (getSystemVariable(source, &var) != 0) {
		result->status = FR_INVALID_PARAMETER;
		return result;
	}

	// Variable lookup worked
	switch (var->type) {
		case MOS_VAR_MACRO: {
			result->result = expandMacro(var->value);
			if (result->result == NULL) {
				result->status = FR_INT_ERR;
			}
			result->type = MOS_VAR_STRING;
			break;
		}
		case MOS_VAR_STRING: {
			result->result = mos_strdup(var->value);
			if (result->result == NULL) {
				result->status = FR_INT_ERR;
			}
			result->type = MOS_VAR_STRING;
			break;
		}
		case MOS_VAR_NUMBER: {
			result->result = var->value;
			result->type = MOS_VAR_NUMBER;
			break;
		}
		default: {
			// In principle we shouldn't find other variables
			result->type = var->type;
			result->status = FR_INT_ERR;
			break;
		}
	}

	return result;
}
