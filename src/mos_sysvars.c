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
		// Call setter function
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
		case MOS_VAR_STRING: {
			*read = *current->source++;
			if (*read == '\0') {
				// end of the string so dispose of this transInfo object
				// and move back to the parent
				*transInfo = current->parent;
				umm_free(current);
				// return next read from parent
				result = gsRead(transInfo, read);
			}
			break;
		}

		case MOS_VAR_NUMBER: {
			// TODO insert logic here for producing next digit of number
			// this will need to use metadata on the current transinfo object
			// to determine which digit we are showing, and how many digits are left
			// algorithm will need to print one character at a time,
			// including potential leading `-` sign
			if (*read == '\0') {
				// end of the number so dispose of this transInfo object
				// and move back to the parent
				*transInfo = current->parent;
				umm_free(current);
				// return next read from parent
				result = gsRead(transInfo, read);
			}
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
						// TODO extract number logic into a function, and add an API for it
						// is this a number?
						int number = 0;
						int base = 10;
						char *endptr = NULL;
						char *start = current->source;
						*end = '\0';
						// number can be decimal, &hex, or base_number
						if (*current->source == '&') {
							base = 16;
							current->source++;
						} else {
							char *underscore = strchr(current->source, '_');
							if (underscore != NULL && underscore > current->source) {
								char *baseEnd;
								*underscore = '\0';
								base = strtol(current->source, &baseEnd, 10);
								if (baseEnd != underscore) {
									// we didn't use all chars before underscore, so invalid base
									base = -1;
								}
								// Move source pointer to the number part
								current->source = underscore + 1;
								*underscore = '_';
							}
						}

						if (base > 1 && base <= 36) {
							number = strtol(current->source, &endptr, base);
						}

						if (endptr != end) {
							// we didn't consume whole string, so it was not a valid number
							// we therefore should interpret it as a variable
							t_mosSystemVariable * var = NULL;
							*end = '\0';
							current->source = start;
							if (getSystemVariable(current->source, &var) == 0) {
								// variable found
								// Set up a new transInfo object
								t_mosTransInfo * newTransInfo = umm_malloc(sizeof(t_mosTransInfo));
								if (newTransInfo == NULL) {
									result = FR_INT_ERR;
								} else {
									newTransInfo->source = var->value;
									newTransInfo->parent = current;
									// TODO - deal with different variable types
									// if variable is a number will need to store some metadata
									// if var is code we need to execute it with a suitable buffer to get the value
									// if var is an expression, we will need to evaluate it (when we can)
									newTransInfo->type = var->type;
									*transInfo = newTransInfo;
									*end = '>';
									result = gsRead(transInfo, read);
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
			// This is used for variable lookup/evaluation
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
