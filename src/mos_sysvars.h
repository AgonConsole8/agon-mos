#ifndef MOS_SYSVARS_H
#define MOS_SYSVARS_H

#include <string.h>

#include "defines.h"
#include "strings.h"
#include "ff.h"
#include "umm_malloc.h"

// TODO renumber these flags and split between number and string
#define EXTRACT_FLAG_DECIMAL_ONLY	(1 << 0)
#define EXTRACT_FLAG_POSITIVE_ONLY	(1 << 1)
#define EXTRACT_FLAG_H_SUFFIX_HEX	(1 << 2)

#define EXTRACT_FLAG_AUTO_TERMINATE		(1 << 0)
#define EXTRACT_FLAG_OMIT_LEADSKIP		(1 << 1)
#define EXTRACT_FLAG_NO_DOUBLEQUOTE		(1 << 2)
#define EXTRACT_FLAG_INCLUDE_QUOTES		(1 << 3)

#define GSTRANS_FLAG_TERMINATE_SPACE	(1 << 0)
#define GSTRANS_FLAG_NO_PIPE			(1 << 1)
#define GSTRANS_FLAG_NO_DOUBLEQUOTE		(1 << 2)
#define GSTRANS_FLAG_NO_TRACE			(1 << 7)

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

typedef struct {
	int (*read)(char *buffer, int *length);
    int (*write)(char *buffer);
} t_mosCodeSystemVariable;

/**
 * Structure to hold information for gs string transformation operations
 * Keeps a pointer to the current source string position, and the parent trans info object
 * Also keeps track of the type of variable we are inserting from
 * Variables that are numbers will be temporarily transformed to a string pointed to by source
 */
typedef struct {
	char active[6];		// Active mosTransInfo indicator
	char * source;		// Pointer to current position in source string
	void * parent;		// Pointer to parent trans object (t_mosTransInfo object) we are inserting
	void * extraData;	// Extra for the current object, as required
	MOSVARTYPE type;	// Type of variable we are inserting from
	BYTE flags;			// Flags for the current operation
} t_mosTransInfo;

typedef struct {
	void * result;
	MOSVARTYPE type;
	int status;
} t_mosEvalResult;

// Utility functions for managing system variables
int		getSystemVariable(char * token, t_mosSystemVariable ** var);
// make system variable object
t_mosSystemVariable * createSystemVariable(char * label, MOSVARTYPE type, void * value);
// insert system variable object
void	insertSystemVariable(t_mosSystemVariable * var, t_mosSystemVariable * before);
// create or update system variable object
int		createOrUpdateSystemVariable(char * label, MOSVARTYPE type, void * value);
// update system variable object
int		updateSystemVariable(t_mosSystemVariable * var, MOSVARTYPE type, void * value);
// delete system variable object
void	removeSystemVariable(t_mosSystemVariable * var);
// find parent system variable object (used for deleting)
t_mosSystemVariable * findParentSystemVariable(t_mosSystemVariable * var);

int		gsInit(void * source, t_mosTransInfo ** transInfoPtr, BYTE flags);
int		gsRead(t_mosTransInfo ** transInfoPtr, char * read);
int		gsTrans(char * source, char * dest, int destLen, int * read, BYTE flags);
// Utility functions for managing gs-trans chains
void	gsDispose(t_mosTransInfo ** transInfo);
void	gsPop(t_mosTransInfo ** transInfo);

bool	extractNumber(char * source, char ** end, char * divider, int * number, BYTE flags);

int		extractString(char * source, char ** end, char * divider, char ** result, BYTE flags);

char *	expandMacro(char * source);

char *	expandVariable(t_mosSystemVariable * var, bool showWriteOnly);

char *	expandVariableToken(char * token);

int		expandPath(char * source, char ** resolvedPath);

t_mosEvalResult * evaluateExpression(char * source);

char *  getArgument(char * source, int argNo, char ** end);

int subsituteArgs(char * template, char * args, char * dest, int length, bool omitRest);

char *	substituteArguments(char * source, char * args, bool omitRest);

#endif MOS_SYSVARS_H
