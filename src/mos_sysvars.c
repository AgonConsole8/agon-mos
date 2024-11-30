// System variables support
//

#include <stdlib.h>
#include <stdio.h>

#include "mos_sysvars.h"
#include "mos_file.h"

t_mosSystemVariable *	mosSystemVariables = NULL;
t_mosTransInfo * 		trackedTransInfo = NULL;

// Get a system variable
// Parameters:
// - pattern: The pattern to search for
// - var: Pointer to the variable to return
// if on entry var is NULL, the search will start from the beginning of the list
// otherwise it will return the first variable after the one pointed to by var
// Returns:
// - var will be updated to point to the variable found, or the variable before the first match
// - 0 if a match was found
// - -1 if no match was found
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
	newVar->type = type;
	if (type == MOS_VAR_MACRO || type == MOS_VAR_STRING) {
		newVar->value = mos_strdup(value);
	} else {
		newVar->value = value;
	}
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

// create and insert/replace system variable object
// intended for system use only - may silently fail if a creation error occurs
int createOrUpdateSystemVariable(char * label, MOSVARTYPE type, void * value) {
	t_mosSystemVariable * var = NULL;
	int result = getSystemVariable(label, &var);
	if (result == 0) {
		// we have found a matching variable
		return updateSystemVariable(var, type, value);
	} else {
		// we have not found a matching variable
		t_mosSystemVariable * newVar = createSystemVariable(label, type, value);
		if (newVar != NULL) {
			insertSystemVariable(newVar, var);
			return FR_OK;
		}
	}
	return FR_INT_ERR;
}

// update system variable object
// returns a status code
int updateSystemVariable(t_mosSystemVariable * var, MOSVARTYPE type, void * value) {
	MOSVARTYPE oldType = var->type;
	if (oldType == MOS_VAR_CODE) {
		// Call setter function, if we have a write function
		if (((t_mosCodeSystemVariable *)var->value)->write != NULL) {
			return ((t_mosCodeSystemVariable *)var->value)->write(value);
		}
		// read-only variables will ignore the update
		return FR_OK;
	}

	var->type = type;
	if (type == MOS_VAR_MACRO || type == MOS_VAR_STRING) {
		char * newValue = mos_strdup(value);
		if (newValue == NULL) {
			return MOS_OUT_OF_MEMORY;
		}
		if (oldType == MOS_VAR_MACRO || oldType == MOS_VAR_STRING) {
			umm_free(var->value);
		}
		var->value = newValue;
	} else {
		var->value = value;
	}
	return FR_OK;
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

int setVarVal(char * name, void * value, char ** actualName, BYTE * type) {
	t_mosSystemVariable * var = *actualName ? mosSystemVariables : NULL;
	int result;
	bool transInput = *type == MOS_VAR_STRING;
	bool freeValue = transInput;

	if (*type > MOS_VAR_LITERAL && *type != 255) {
		// Catch invalid types, and prevent (for now) calling with type of MOS_VAR_CODE
		return FR_INVALID_PARAMETER;
	}

	if (*type == MOS_VAR_EXPANDED) {
		t_mosEvalResult * evalResult = evaluateExpression(value);
		if (evalResult == NULL) {
			return FR_INT_ERR;
		}
		if (evalResult->status != FR_OK) {
			result = evalResult->status;
			umm_free(evalResult);
			return result;
		}
		*type = evalResult->type;
		value = evalResult->result;
		if (*type == MOS_VAR_STRING) {
			freeValue = true;
		}
	}

	// When we have an actualName, we are looking for the next occurrence of the variable matching name pattern
	while (var && var->label != *actualName) {
		var = var->next;
	}
	result = getSystemVariable(name, &var);

	// If type passed to command is -1 (255) then we are deleting the variable
	if (*type == 255) {
		if (result == 0) {
			if (var->type != MOS_VAR_CODE) {
				removeSystemVariable(var);
			}
			return FR_OK;
		}
		return FR_INVALID_NAME;
	}

	if (transInput) {
		value = expandMacro(value);
		if (value == NULL) {
			return FR_INT_ERR;
		}
	}
	if (*type == MOS_VAR_LITERAL) {
		*type = MOS_VAR_STRING;
	}

	if (result == -1) {
		// Variable wasn't found, so we need to create it
		t_mosSystemVariable * newVar = createSystemVariable(name, *type, value);
		if (newVar == NULL) {
			return FR_INT_ERR;
		}
		// `var` will be our insertion point
		insertSystemVariable(newVar, var);
		var = newVar;
		result = FR_OK;
	} else {
		// Variable was found, so we need to update it with the new type and value
		result = updateSystemVariable(var, *type, value);
	}
	*type = var->type;
	*actualName = var->label;
	if (freeValue) {
		umm_free(value);
	}
	return result;
}

int readVarVal(char * namePattern, void * value, char ** actualName, int * length, BYTE * typeFlag) {
	t_mosSystemVariable * var = mosSystemVariables;
	int result;
	int bufferLen = *length;
	bool expand = *typeFlag == 3;

	*length = 0;

	// When we have an actualName, we are looking for the next occurrence of the variable matching name pattern
	while (var && var->label != *actualName) {
		var = var->next;
	}
	result = getSystemVariable(namePattern, &var);

	if (result == -1) {
		return FR_INVALID_NAME;
	}

	*actualName = var->label;
	*typeFlag = var->type;
	if (var->type == MOS_VAR_CODE) {
		// reading a code variable auto-expands
		expand = true;
	}

	result = FR_OK;
	if (expand) {
		char * expanded = expandVariable(var, true);
		if (expanded == NULL) {
			// Couldn't expand
			return FR_INT_ERR;
		}
		*length = strlen(expanded);
		if (value != NULL) {
			strncpy(value, expanded, bufferLen);
			if (bufferLen < *length) {
				result = MOS_OUT_OF_MEMORY;
			}
		}
		umm_free(expanded);
	} else {
		if (var->type == MOS_VAR_MACRO || var->type == MOS_VAR_STRING) {
			*length = strlen(var->value);
			if (value != NULL) {
				strncpy(value, var->value, bufferLen);
				if (bufferLen < *length) {
					result = MOS_OUT_OF_MEMORY;
				}
			}
		} else {
			*length = 3;	// Numbers are 3 byte values
			if (value != NULL) {
				*(int *)value = (int)var->value;
			}
		}
	}

	return result;
}


int gsInit(void * source, t_mosTransInfo ** transInfoPtr, BYTE flags) {
	t_mosTransInfo * transInfo;

	if (transInfoPtr == NULL) {
		return FR_INVALID_PARAMETER;
	}

	// Set up a t_mosTransInfo object
	transInfo = umm_malloc(sizeof(t_mosTransInfo));
	if (transInfo == NULL) {
		return MOS_OUT_OF_MEMORY;
	}
	// NB "active" provides crude detection of a valid (non-expired) transInfo object
	// it is not foolproof, as a new allocation of a new transInfo object _could_
	// get allocated to the same address as one previously in use, but this is unlikely
	sprintf(transInfo->active, "TInfo");
	transInfo->source = source;
	transInfo->parent = NULL;
	transInfo->type = MOS_VAR_MACRO;
	transInfo->flags = flags;

	if (!(flags & GSTRANS_FLAG_NO_DOUBLEQUOTE) && *((char*)source) == '"') {
		// We have a double-quoted value, so we should skip the first character
		transInfo->source++;
	} else {
		// Not actually a double-quote enclosed source, so we should set the flag
		transInfo->flags |= GSTRANS_FLAG_NO_DOUBLEQUOTE;
	}

	if (!(flags & GSTRANS_FLAG_NO_TRACE)) {
		// This is a tracked trans, so dispose of any previous one
		if (trackedTransInfo != NULL) {
			gsDispose(&trackedTransInfo);
		}
		trackedTransInfo = transInfo;
	}

	*transInfoPtr = transInfo;
	return FR_OK;
}

int gsRead(t_mosTransInfo ** transInfo, char * read) {
	// read next char from our source,
	// which should either be of type MOS_VAR_STRING, MOS_VAR_NUMBER, or MOS_VAR_MACRO
	// our info may have metadata to help us read the next char, such as MOS_VAR_NUMBER
	// on reaching end of current item, we need to dispose of it and move back to the parent
	t_mosTransInfo * current = *transInfo;
	int result = FR_OK;

	if (read == NULL) {
		return FR_INVALID_PARAMETER;
	}
	if (current == NULL) {
		// We have reached the end of the chain
		return FR_OK;
	}
	if (strcmp(current->active, "TInfo") != 0) {
		// This isn't a valid transInfo object
		return FR_INT_ERR;
	}

	*read = '\0';
	if (!current->parent && (current->flags & GSTRANS_FLAG_TERMINATE_SPACE) && isspace(*current->source)) {
		gsPop(transInfo);
		return FR_OK;
	}

	// Do transformation based on type, if we need to
	switch (current->type) {
		case MOS_VAR_LITERAL:
		case MOS_VAR_STRING:
		case MOS_VAR_CODE:
		{
			*read = *current->source++;
			if (*read == '\0') {
				// end of the string so pop current transInfo object
				gsPop(transInfo);
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
				gsPop(transInfo);
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
				case '"': {
					if (current->flags & GSTRANS_FLAG_NO_DOUBLEQUOTE) {
						// we should treat this as a normal character
						break;
					}
					// otherwise we terminate the whole translation
					*read = '\0';
					gsDispose(transInfo);
					result = gsRead(transInfo, read);
					break;
				}
				case '\0': {
					// end of the macro - move back to the parent
					gsPop(transInfo);
					result = gsRead(transInfo, read);
					break;
				}

				case '|': {
					if (current->flags & GSTRANS_FLAG_NO_PIPE) {
						// we should treat this as a normal character
						break;
					}
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
					} else if (*current->source == '|') {
						// prints a pipe character
						*read = '|';
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
						if (!extractNumber(current->source, &end, ">", &number, 0)) {
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
									gsDispose(transInfo);
									result = MOS_OUT_OF_MEMORY;
								} else {
									sprintf(newTransInfo->active, "TInfo");
									newTransInfo->source = var->value;
									newTransInfo->parent = current;
									newTransInfo->type = var->type;
									newTransInfo->flags = current->flags;
									*transInfo = newTransInfo;
									if (!(current->flags & GSTRANS_FLAG_NO_TRACE)) {
										trackedTransInfo = newTransInfo;
									}
									*end = '>';
									current->source = end + 1;
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
											char * newValue = expandVariable(var, false);
											if (!newValue) {
												// Variable couldn't be read for some reason
												*read = '\0';
												gsDispose(transInfo);
												return MOS_BAD_STRING;
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
// - MOS_OUT_OF_MEMORY if memory allocation failed
//
int gsTrans(char * source, char * dest, int destLen, int * read, BYTE flags) {
	t_mosTransInfo * transInfo;
	int result;
	int remaining = destLen;
	char c;

	if (source == NULL || source == dest || read == NULL) {
		return FR_INVALID_PARAMETER;
	}
	// belt and braces - ensure we definitely don't write to NULL
	if (dest == NULL) {
		remaining = 0;
	}
	*read = 0;

	result = gsInit(source, &transInfo, flags | GSTRANS_FLAG_NO_TRACE);
	if (result != FR_OK) {
		return result;
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

void gsDispose(t_mosTransInfo ** transInfoPtr) {
	t_mosTransInfo * transInfo = *transInfoPtr;

	if (transInfo == NULL) {
		return;
	}
	if (!(transInfo->flags & GSTRANS_FLAG_NO_TRACE)) {
		// This is a tracked transInfo chain, so we must remove tracking
		trackedTransInfo = NULL;
	}
	while (transInfo != NULL) {
		t_mosTransInfo * next = transInfo->parent;
		sprintf(transInfo->active, "DEAD");
		umm_free(transInfo);
		transInfo = next;
	}
	*transInfoPtr = NULL;
}

void gsPop(t_mosTransInfo ** transInfo) {
	t_mosTransInfo * current = *transInfo;

	if (!current) {
		return;
	}

	if (current->type == MOS_VAR_CODE) {
		// free cached string, pointer is stored in extraData
		umm_free(current->extraData);
	}
	*transInfo = current->parent;
	sprintf(current->active, "DEAD");
	umm_free(current);

	if (!(current->flags & GSTRANS_FLAG_NO_TRACE)) {
		trackedTransInfo = *transInfo;
	}
}

// Extract a number from a string
// Parameters:
// - source: The source string
// - end: The end point in the source string for extraction,
//		or null for whole string
//      pointing to source means we will update end to point to the end of the number
// - divider: The token divider string - null for default (space)
// - number: Pointer to an integer to store the extracted number
// - flags: Flags to control extraction
// Returns:
// - TRUE if successful
// - FALSE if the number could not be extracted, or the string was not fully consumed
// - end will be updated to point to the end of the number
//   (will not consume divider characters)
//   TODO work thru logic around when "end" gets updated
//
bool extractNumber(char * source, char ** end, char * divider, int * number, BYTE flags) {
	int base = 10;
	char *endptr = NULL;
	char *parseEnd = NULL;
	char *start = source;
	char lastChar = '\0';
	int value = 0;

	if (divider == NULL) {
		divider = " ";
	}

	// skip our source past any start token dividers
	start += mos_strspn(start, divider);

	if (end == NULL || *end == NULL || *end == source) {
		// No explicit end provided, so set to next divider, or string end
		endptr = start + mos_strcspn(start, divider);
	} else {
		endptr = *end;
	}
	lastChar = *endptr;
	*endptr = '\0';

	if (strlen(start) == 0) {
		*endptr = lastChar;
		return false;
	}

	// number can be decimal, &hex, or base_number
	if (flags & EXTRACT_FLAG_H_SUFFIX_HEX) {
		// check for 'h' suffix
		if (*(endptr - 1) == 'h') {
			base = 16;
			endptr--;
		}
	}
	if (*start == '&') {
		base = 16;
		start++;
	} else if (*start == '0' && ((*(start + 1) == 'x') || (*(start + 1) == 'X'))) {
		base = 16;
		start += 2;
	} else if (base != 16) {
		char *underscore = strchr(start, '_');
		if (underscore != NULL && underscore > start) {
			char *baseEnd;
			*underscore = '\0';
			base = strtol(start, &baseEnd, 10);
			if (baseEnd != underscore) {
				// we didn't use all chars before underscore, so invalid base
				base = -1;
			}
			// Move start pointer to the number part
			start = underscore + 1;
			*underscore = '_';
		}
	}

	if ((flags & EXTRACT_FLAG_DECIMAL_ONLY) && base != 10) {
		*endptr = lastChar;
		return false;
	}

	if (base > 1 && base <= 36) {
		value = strtol(start, &parseEnd, base);
	} else {
		return false;
	}

	*endptr = lastChar;

	if ((parseEnd < endptr) || (flags & EXTRACT_FLAG_POSITIVE_ONLY && value < 0)) {
		// we didn't consume whole string, or negative found for positive only
		if (*end == source) {
			// update our end pointer to point to where we reached
			*end = parseEnd;
		}
		return false;
	}

	if (end != NULL) {
		*end = parseEnd;
	}

	*number = value;
	return true;
}

// Extract a string from the source, with the given divider
// Parameters:
// - source: Pointer to the source string
// - end: Pointer to pointer to return end of string
// - divider: The token divider string - null for default (space)
// - result: Pointer to pointer for result string
// - flags: Flags to control extraction
// Returns:
// - status code
// end will point to character after the end of the extracted string
//
int extractString(char * source, char ** end, char * divider, char ** result, BYTE flags) {
	char * start = source;
	char * endptr = NULL;
	bool findEndQuotes = false;

	if (result == NULL) {
		return FR_INVALID_PARAMETER;
	}

	if (divider == NULL) {
		divider = " ";
	}
	if (!(flags & EXTRACT_FLAG_OMIT_LEADSKIP)) {
		// skip our source past any start token dividers
		start = start + mos_strspn(start, divider);
	}

	if (!(flags & EXTRACT_FLAG_NO_DOUBLEQUOTE) && *start == '"') {
		// printf("found double quote\n");
		// we have a double-quoted value, so we should skip the first character
		if (!(flags & EXTRACT_FLAG_INCLUDE_QUOTES)) {
			start++;
		}
		findEndQuotes = true;
	}

	// printf("findEndQuotes: %s, flag was %s\n", findEndQuotes ? "true" : "false", flags & EXTRACT_FLAG_NO_DOUBLEQUOTE ? "true" : "false");

	if (findEndQuotes) {
		// iterate thru string to find our end quote
		// NB quotes can be escaped with \" or ""
		endptr = start;
		if (flags & EXTRACT_FLAG_INCLUDE_QUOTES) {
			endptr++;
		}
		while (*endptr != '\0') {
			if (*endptr == '"') {
				// skip over escaped quotes
				if (*(endptr + 1) == '"') {
					endptr++;
				} else if (*(endptr - 1) != '\\') {
					// found end quote
					break;
				}
			}
			endptr++;
		}
		if (*endptr == '\0') {
			// no end quote found
			// printf("no end quote found\n");
			return MOS_BAD_STRING;
		}
		// if our next character is neither a divider nor end of string, then we have a bad string
		if (*(endptr + 1) != '\0' && strchr(divider, *(endptr + 1)) == NULL) {
			return MOS_BAD_STRING;
		}
		if (flags & EXTRACT_FLAG_INCLUDE_QUOTES) {
			endptr++;
		}
	} else {
		// find the end of the string by next divider
		endptr = start + mos_strcspn(start, divider);
	}

	if (*endptr != '\0' && (flags & EXTRACT_FLAG_AUTO_TERMINATE)) {
		*endptr = '\0';
		endptr++;
	}

	*result = start;
	if (end != NULL) {
		*end = endptr;
	}

	if (start == endptr) {
		return FR_INVALID_PARAMETER;
	}

	return FR_OK;
}

int escapeString(char * source, char * dest, int * length) {
	char * start = source;
	int result = FR_OK;
	int destLen = 1;
	bool countOnly = dest == NULL;

	if (source == NULL) {
		return FR_INVALID_PARAMETER;
	}

	// we are counting the length of the escaped string
	while (*start != '\0') {
		if (*start < 32 || *start == 127 || *start == '|') ++destLen;
		destLen++;
		start++;
	}

	if (dest != NULL) {
		int remaining = *length;
		start = source;

		// we are escaping the string
		if (remaining <= 0) {
			return FR_INVALID_PARAMETER;
		}

		while (*start) {
			if (*start < 32 || *start == 127 || *start == '|') {
				if (remaining <= 2) {
					result = MOS_OUT_OF_MEMORY;
					*dest++ = '\0';
					break;
				}
				*dest++ = '|';
				if (*start == 0x7F) {
					*dest++ = '?';
				} else if (*start == '|') {
					*dest++ = '|';
				} else {
					*dest++ = *start + 64;
				}
				remaining -= 2;
			} else {
				*dest++ = *start;
				remaining--;
				if (remaining == 1) {
					*dest = '\0';
					break;
				}
			}
			start++;
		}
		if (start < source + strlen(source) - 1) {
			result = MOS_OUT_OF_MEMORY;
		}
	}

	*length = destLen;
	return result;
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

	result = gsTrans(source, NULL, 0, &read, GSTRANS_FLAG_NO_DOUBLEQUOTE);
	if (result != FR_OK) {
		return NULL;
	}
	dest = umm_malloc(read + 1);
	if (dest == NULL) {
		return NULL;
	}
	result = gsTrans(source, dest, read, &read, GSTRANS_FLAG_NO_DOUBLEQUOTE);
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
char * expandVariable(t_mosSystemVariable * var, bool showWriteOnly) {
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
		// get length of our code read result
		if (((t_mosCodeSystemVariable *)var->value)->read == NULL) {
			if (showWriteOnly) {
				return mos_strdup("(write only)");
			}
			return NULL;
		}
		if (((t_mosCodeSystemVariable *)var->value)->read(NULL, &len) != FR_OK) {
			return NULL;
		}
		newValue = umm_malloc(len);
		if (newValue == NULL) {
			return NULL;
		}
		// get result value
		if (((t_mosCodeSystemVariable *)var->value)->read(newValue, &len) != FR_OK) {
			umm_free(newValue);
			return NULL;
		}
		return newValue;
	}
	return NULL;
}

char * expandVariableToken(char * token) {
	t_mosSystemVariable * var = NULL;
	int result = getSystemVariable(token, &var);
	if (result != 0) {
		return NULL;
	}
	return expandVariable(var, false);
}

int expandPath(char * source, char ** resolvedPath) {
	// Expand path, and resolve it
	int result = FR_INT_ERR;
	char * path = NULL;
	char * expanded = expandMacro(source);
	if (expanded == NULL) {
		return result;
	}
	result = getResolvedPath(expanded, resolvedPath);
	umm_free(expanded);
	return result;
}

// For this to work as an API, it will need to change
// it should return a status result value, and accept in parameters to allow this routine to return
// both a pointer to a buffer for the result value, and a pointer to an integer for the the type of the result
// the returned type should be a number or a string only
// the result value should be a pointer to the result, or the number itself
//
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
	if (extractNumber(source, NULL, NULL, &number, 0)) {
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
	if (var->type == MOS_VAR_NUMBER) {
		result->type = MOS_VAR_NUMBER;
		result->result = var->value;
	} else {
		result->result = expandVariable(var, false);
		if (result->result == NULL) {
			result->status = FR_INT_ERR;
		}
		result->type = MOS_VAR_STRING;
	}

	return result;
}

// Get an argument from a source string
//
char * getArgument(char * source, int argNo, char ** end) {
	char * result = source;
	char * divider = " ";
	int argCount = 0;
	char * scanFrom = result;

	if (end != NULL) {
		*end = NULL;
	}

	while (argCount <= argNo) {
		if (extractString(scanFrom, &scanFrom, divider, &result, EXTRACT_FLAG_INCLUDE_QUOTES)) {
			return NULL;
		}
		argCount++;
	}

	if (end != NULL) {
		*end = scanFrom;
	}

	return result;
}

// Substitute arguments into a template string
// Parameters:
// - template: The template string
// - args: The arguments string
// - dest: The destination buffer (or NULL to just calculate the length)
// - length: The length of the destination buffer
// - omitRest: If true, any unused arguments will be omitted
// Returns:
// - The length of the substituted string
//
int substituteArgs(char * template, char * args, char * dest, int length, bool omitRest) {
	char * end = template + strlen(template);
	char * argument;
	int argNo;
	int argLen = 0;
	int maxArg = -1;
	int size = 0;
	int destRemaining = length;
	bool copying = (dest != NULL);

	while (template < end) {
		if (*template == '%') {
			argument = NULL;
			template++;
			if (*template == 's') {
				template++;
				argument = args;
				argLen = strlen(args);
				maxArg = 99;	// we have used all arguments
			} else if (*template == '*' && *(template + 1) >= '0' && *(template + 1) <= '9') {
				template++;	// skip the *
				argNo = *template - '0';
				template++;	// skip the number
				argument = getArgument(args, argNo, NULL);
				if (argument != NULL) {
					argLen = strlen(argument);
				}
				maxArg = 99;	// count a rest arg as all arguments
			} else if (*template >= '0' && *template <= '9') {
				char * argEnd = NULL;
				argNo = *template - '0';
				template++;	// skip the number
				argument = getArgument(args, argNo, &argEnd);
				if (argument != NULL) {
					argLen = argEnd - argument;
				}
				if (argNo > maxArg) {
					maxArg = argNo;
				}
			} else {
				// not a valid argument, so just copy the % and move on
				size++;
				if (copying) {
					*dest++ = '%';
					destRemaining--;
				}
				if (*template == '%') {
					template++;
				}
			}
			if (argument != NULL) {
				size += argLen;
				if (copying) {
					strncpy(dest, argument, argLen < destRemaining ? argLen : destRemaining);
					dest += argLen;
					destRemaining -= argLen;
				}
			}
		} else {
			size++;
			if (copying) {
				*dest++ = *template;
				destRemaining--;
			}
			template++;
		}
		if (copying && destRemaining <= 0) {
			// we have run out of space in the destination buffer
			copying = false;
		}
	}

	// Work out if we have any unused arguments to append
	if (!omitRest && maxArg < 99) {
		argument = getArgument(args, maxArg + 1, NULL);
		if (argument != NULL) {
			argLen = strlen(argument);
			size += argLen;
			if (copying) {
				*dest++ = ' ';
				destRemaining--;
				strncpy(dest, argument, argLen < destRemaining ? argLen : destRemaining);
			}
			copying = false;
		}
	}
	if (copying) {
		*dest = '\0';
	}

	return ++size;
}


// wrapper call for substituteArgs to return a newly allocated string
//
char * substituteArguments(char * source, char * args, bool omitRest) {
	char * dest;
	int size = substituteArgs(source, args, NULL, 0, omitRest);
	if (size == 0) {
		return NULL;
	}
	dest = umm_malloc(size + 1);
	if (dest == NULL) {
		return NULL;
	}
	substituteArgs(source, args, dest, size, omitRest);
	dest[size + 1] = '\0';		// is this needed??
	return dest;
}
