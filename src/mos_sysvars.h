#ifndef MOS_SYSVARS_H
#define MOS_SYSVARS_H

#include <string.h>

#include "defines.h"
#include "strings.h"
#include "ff.h"
#include "umm_malloc.h"

/**
 * MOS system variable types
 */
typedef enum {
	MOS_VAR_STRING = 0,	// String, which will be GSTrans'd before being stored
	MOS_VAR_NUMBER,		// Integer (24-bit, as we're on an eZ80)
	MOS_VAR_MACRO,		// String that will be GSTrans'd each time it is used
	MOS_VAR_EXPANDED,	// Expression which will be evaluated before being stored
	MOS_VAR_LITERAL,	// Literal string, no GSTrans
	MOS_VAR_CODE = 16,	// Code block, with offset 0 pointing to read, offset 4 to write
} MOSVARTYPE;

typedef struct {
	char * label;
	MOSVARTYPE type;
	void * value;
	void * next;
} t_mosSystemVariable;

/**
 * Structure to hold information for gs string transformation operations
 * Keeps a pointer to the current source string position, and the parent trans info object
 * Also keeps track of the type of variable we are inserting from
 * Variables that are numbers will be temporarily transformed to a string pointed to by source
 */
typedef struct {
	char * source;		// Pointer to current position in source string
	void * parent;		// Pointer to parent trans object (t_mosTransInfo object) we are inserting
	MOSVARTYPE type;	// Type of variable we are inserting from
} t_mosTransInfo;

// Utility functions for managing system variables
int		getSystemVariable(char * token, t_mosSystemVariable ** var);
// make system variable object
t_mosSystemVariable * createSystemVariable(char * label, MOSVARTYPE type, void * value);
// insert system variable object
void	insertSystemVariable(t_mosSystemVariable * var, t_mosSystemVariable * before);
// update system variable object
void	updateSystemVariable(t_mosSystemVariable * var, MOSVARTYPE type, void * value);
// delete system variable object
// int		deleteSystemVariable(t_mosSystemVariable * var);
// find parent system variable object
// t_mosSystemVariable * findParentSystemVariable(t_mosSystemVariable * var);


t_mosTransInfo * gsInit(void * source, void * parent);
int		gsRead(t_mosTransInfo ** transInfo, char * read);
int		gsTrans(char * source, char * dest, int destLen, int * read);

char *	expandMacro(char * source);

#endif MOS_SYSVARS_H
