/*
 * $Source: /afs/dev.mit.edu/source/repository/athena/bin/delete/pattern.c,v $
 * $Author: jik $
 *
 * This program is part of a package including delete, undelete,
 * lsdel, expunge and purge.  The software suite is meant as a
 * replacement for rm which allows for file recovery.
 * 
 * Copyright (c) 1989 by the Massachusetts Institute of Technology.
 * For copying and distribution information, see the file "mit-copyright.h."
 */

#if (!defined(lint) && !defined(SABER))
     static char rcsid_pattern_c[] = "$Header: /afs/dev.mit.edu/source/repository/athena/bin/delete/pattern.c,v 1.13 1989-11-22 21:27:27 jik Exp $";
#endif

#include <stdio.h>
#include <sys/types.h>
#include <sys/dir.h>
#include <sys/param.h>
#include <strings.h>
#include <sys/stat.h>
#include <errno.h>
#include <com_err.h>
#include "directories.h"
#include "pattern.h"
#include "util.h"
#include "undelete.h"
#include "shell_regexp.h"
#include "mit-copyright.h"
#include "delete_errs.h"
#include "errors.h"
#include "stack.h"

extern char *realloc();
extern int errno;
extern char *whoami;

void free_list();


/*
 * add_arrays() takes pointers to two arrays of char **'s and their
 * lengths, merges the two into the first by realloc'ing the first and
 * then free's the second's memory usage.
 */  
int add_arrays(array1, num1, array2, num2)
char ***array1, ***array2;
int *num1, *num2;
{
     int counter;
     
     *array1 = (char **) realloc((char *) *array1, (unsigned)
				 (sizeof(char *) * (*num1 + *num2)));
     if (! *array1) {
	  set_error(errno);
	  error("realloc");
	  return error_code;
     }
     for (counter = *num1; counter < *num1 + *num2; counter++)
	  *(*array1 + counter) = *(*array2 + counter - *num1);
     free ((char *) *array2);
     *num1 += *num2;
     return 0;
}






/*
 * Add a string to a list of strings.
 */
int add_str(strs, num, str)
char ***strs;
int num;
char *str;
{
     char **ary;

     ary = *strs = (char **) realloc((char *) *strs, (unsigned)
				     (sizeof(char *) * (num + 1)));
     if (! *strs) {
	  set_error(errno);
	  error("realloc");
	  return error_code;
     }
     ary[num] = Malloc((unsigned) (strlen(str) + 1));
     if (! ary[num]) {
	  set_error(errno);
	  error("Malloc");
	  return error_code;
     }
     (void) strcpy(ary[num], str);
     return 0;
}





/*
 * Find_matches will behave unpredictably if you try to use it to find
 * very strange combinations of file types, for example only searching
 * for undeleted files in the top-level directory, while searching
 * recursively for deleted files.  Basically, there are some conflicts
 * between options that I don't list here because I don't think I'll
 * ever need to use those combinations.
 */
/*
 * Function: find_matches(char *name, int *num_found, char ***found,
 *   			  int options)
 *
 * Requires: name points to a NULL-terminated string, representing a
 *   filename pattern with regular filename characters, path
 *   separators and shell wildcard characters []*?; num_found points
 *   to a valid int memory storage location; found points to a valid
 *   char ** memory storage location.
 *
 * Effects: Returns a list of all the files in the file hierarchy that
 *   match the options specified in options and that match name.
 *   Options are:
 *
 *   FIND_UNDELETED search for and return undeleted files
 * 
 *   FIND_DELETED search for and return deleted files
 *
 *   FIND_CONTENTS means that if matches are directories (or links to
 *     directories), the contents of the directory should be matched
 *     in addition to the directory itself
 * 
 *   RECURS_FIND_DELETED to search all undeleted subdirectories
 *     recursively of matched directories looking for deleted files
 *
 *   RECURS_FIND_UNDELETED to search all undeleted subdirectories
 *     recursively of matched directories looking for undeleted files
 * 
 *   RECURS_DELETED to recursively return all contents of deleted
 *     directories in addition to the directories themselves
 *   
 *   FOLLW_LINKS to pursue symlinks to directories and continue down
 *     the referenced directories when searching recursively (if the
 *     initial string is an undeleted symlink it is always traversed;
 *     deleted symlinks are never traversed)
 *   
 *   FOLLW_MOUNTPOINTS to traverse mount points when searching
 *     recursively (if the initial string is a mountpoint it is always
 *     traversed)
 *
 *   FIND_DOTFILES forces the system to recognize dot files instead of
 *     discarding them when looking for files
 *
 *   If the first character of name is '/', the search is conducted
 *   absolutely from the root of the hierarchy; else, it is conducted
 *   relative to the current working directory.  The number of
 *   matching files is returned in *num_found, and a list of file
 *   names is returned in *found.  If there are no errors, the return
 *   value is 0; else the return value represents the error code of
 *   the error which occurred.  No matter how many file names are
 *   returned, the memory location addressed in *found is a valid
 *   pointer according to Malloc() and can be released using free()
 *   safely.  However, if an error value is returned, the caller
 *   should not attempt to use the values stored in *num_found or
 *   *found.
 *
 * Modifies: *num_found, *found.
 */
int find_matches(name, num_found, found, options)
char *name;
int *num_found;
char ***found;
int options;
{
     char 	**matched_files, **return_files, **recurs_files;
     int 	num_matched_files = 0, num_return_files = 0,
                num_recurs_files = 0;
     int	retval;
     int	i;
     int	match_options = 0;

     match_options = options & (FIND_DELETED | FIND_UNDELETED);
     if (options & (RECURS_FIND_DELETED | RECURS_FIND_UNDELETED |
		    FIND_CONTENTS))
	  match_options |= FIND_UNDELETED;
     
     if (! match_options) {
	  set_error(PAT_NO_FILES_REQUESTED);
	  error("find_matches");
	  return error_code;
     }
     
     retval = do_match(name, &num_matched_files, &matched_files,
		       match_options & FIND_UNDELETED,
		       match_options & FIND_DELETED);
     if (retval) {
	  error(name);
	  return retval;
     }
     if (num_matched_files == 0) {
	  *num_found = num_matched_files;
	  *found = matched_files;
	  return 0;
     }

     if (options & RECURS) {
	  return_files = (char **) Malloc(0);
	  if (! return_files) {
	       set_error(errno);
	       error("Malloc");
	       return error_code;
	  }
	  num_return_files = 0;

	  for (i = 0; i < num_matched_files; i++) {

	       retval = do_recurs(matched_files[i], &num_recurs_files,
				  &recurs_files, options);
	       if (retval) {
		    error(matched_files[i]);
		    return retval;
	       }

	       if (num_recurs_files) {
		    retval = add_arrays(&return_files, &num_return_files,
					&recurs_files, &num_recurs_files);
		    if (retval) {
			 error("add_arrays");
			 return retval;
		    }
	       }
	       
	       if (is_deleted(matched_files[i])) {
		    if (options & FIND_DELETED) {
			 retval = add_str(&return_files, num_return_files,
					  matched_files[i]);
			 if (retval) {
			      error("add_str");
			      return retval;
			 }
			 num_return_files++;
		    }
	       }
	       else if (options & FIND_UNDELETED) {
		    retval = add_str(&return_files, num_return_files,
				     matched_files[i]);
		    if (retval) {
			 error("add_str");
			 return retval;
		    }
		    num_return_files++;
	       }
	  }
	  free_list(matched_files, num_matched_files);
	  *num_found = num_return_files;
	  *found = return_files;
     }
     else {
	  *num_found = num_matched_files;
	  *found = matched_files;
     }

     return 0;
}




	  
	  
		    
	       
#define string_push(str)\
	  strsize = strlen(str);\
	  retval = push(str, strsize);\
	  if (! retval)\
	       retval |= push(&strsize, sizeof(int));\
	  if (retval) {\
	       error("push");\
	       (void) popall();\
	       return retval;\
	  }
#define string_pop(str)\
	  retval = pop(&strsize, sizeof(int));\
	  if (! retval)\
	       retval = pop(str, strsize);\
	  if (! retval)\
	       str[strsize] = '\0'
	  
	  




/*
 * Function: do_match(char *name, int *num_found, char ***found,
 * 		      Boolean match_undeleted, Boolean match_deleted)
 *
 * Requires: name points to a NULL-terminated string, representing a
 *   filename pattern with regular filename characters, path
 *   separators and shell wildcard characters []*?; num_found points
 *   to a valid int memory storage location; found points to a valid
 *   char ** memory storage location.
 *
 * Effects: Returns a list of all the files in the file hierarchy that
 *   match name.  If match_undeleted is true, will return undeleted
 *   files that match; if match_deleted is true, will return
 *   deleted_files that match.  If the first character of name is '/',
 *   the search is conducted absolutely from the root of the
 *   hierarchy; else, it is conducted relative to the current working
 *   directory.  The number of matching files is returned in
 *   *num_found, and a list of file names is returned in *found.  If
 *   there are no errors, the return value is 0; else the return value
 *   represents the error code of the error which occurred.  No matter
 *   how many file names are returned, the memory location addressed
 *   in *found is a valid pointer according to Malloc() and can be
 *   released using free() safely.  However, if an error value is
 *   returned, the caller should not attempt to use the values stored
 *   in *num_found or *found.
 *
 * Modifies: *num_found, *found.
 *
 * Algorithm:
 *
 * start:
 *   base = "" or "/",
 *   name = name or name + 1
 *   initialze found and num_found
 *   dirp = opendir(base)
 *   first = firstpart(name, rest) (assigns rest as side-effect)
 *   if (! *first) {
 *     add string to list if appropriate
 *     return
 * 
 * loop:
 *   dp = readdir(dirp)
 *   if (! dp) goto updir
 *   compare dp->d_name to first -- match?
 *     yes - goto downdir
 *     no - are we looking for deleted and is dp->d_name deleted?
 *       yes - compare undeleted dp->d_name to first -- match?
 *         yes - goto downdir
 *         no - goto loop
 *       no - goto loop
 *
 * downdir:
 *   save dirp, rest, first and base on stack
 *   first = firstpart(rest, rest)
 *   base = dp->d_name appended to base
 *   is first an empty string?
 *      yes - put back dirp, rest, first, base
 *            goto loop
 *   try to open dir base - opens?
 *      yes - goto loop
 *      no - is the error ENOTDIR?
 *	       yes - don't worry about it
 * 	       no - report the error
 * 	     restore dirp, rest, first, base from stack
 *           goto loop
 *
 * updir:
 *   close dirp
 *   restore base, rest, first from stack
 *   STACK_EMPTY?
 *     yes - return from procedure with results
 *   restore dirp from stack
 *   goto loop
 */
int do_match(name, num_found, found, match_undeleted, match_deleted)
char *name;
int *num_found;
char ***found;
Boolean match_undeleted, match_deleted;
{
     char base[MAXPATHLEN];
     struct direct *dp;
     DIR *dirp;
     char first[MAXNAMLEN], rest[MAXPATHLEN];
     int retval;
     int strsize;
     
#ifdef DEBUG
     printf("do_match: looking for %s\n", name);
#endif

     /* start: */
     
     if (*name == '/') {
	  *base = '/';
	  *(base + 1) = '\0';
	  name++;
     }
     else 
	  *base = '\0';

     *found = (char **) Malloc(0);
     if (! *found) {
	  set_error(errno);
	  error("Malloc");
	  return error_code;
     }
     *num_found = 0;
     
     dirp = opendir(base);
     if (! dirp) {
	  set_error(errno);
	  error(base);
	  return error_code;
     }
     (void) strcpy(first, firstpart(name, rest));
     if ((! *first) && (match_undeleted)) {
	  retval = add_str(found, *num_found, base);
	  if (retval) {
	       error("add_str");
	       (void) popall();
	       return retval;
	  }
	  (*num_found)++;
	  return 0;
     }
     
     while (1) {
	  dp = readdir(dirp);
	  if (! dp) goto updir;

	  retval = reg_cmp(first, dp->d_name);
	  if (retval < 0) {
	       error("reg_cmp");
	       goto updir;
	  }

	  if (retval == REGEXP_MATCH) goto downdir;

	  if (is_deleted(dp->d_name) && match_deleted) {
	       retval = reg_cmp(first, &dp->d_name[2]);
	       if (retval < 0) {
		    error("reg_cmp");
		    goto updir;
	       }

	       if (retval == REGEXP_MATCH)
		    goto downdir;
	       else
		    continue;
	  }
	  else
	       continue;

     downdir:
	  retval = push(&dirp, sizeof(DIR *));
	  if (retval) {
	       error("push");
	       (void) popall();
	       return retval;
	  }
	  string_push(first);
	  string_push(rest);
	  string_push(base);
	  (void) strcpy(base, append(base, dp->d_name));
	  (void) strcpy(first, firstpart(rest, rest));
	  if (! *first) {
	       if (is_deleted(lastpart(base))) {
		    if (match_deleted) {
			 retval = add_str(found, *num_found, base);
			 if (retval) {
			      error("add_str");
			      (void) popall();
			      return retval;
			 }
			 (*num_found)++;
		    }
	       }
	       else if (match_undeleted) {
		    retval = add_str(found, *num_found, base);
		    if (retval) {
			 error("add_str");
			 (void) popall();
			 return retval;
		    }
		    (*num_found)++;
	       }
	       string_pop(base);
	       string_pop(rest);
	       string_pop(first);
	       (void) pop(&dirp, sizeof(DIR *));
	       continue;
	  }
	  
	  dirp = opendir(base);
	  if (! dirp) {
	       if (errno != ENOTDIR) {
		    set_error(errno);
		    error(base);
	       }
	       string_pop(base);
	       string_pop(rest);
	       string_pop(first);
	       (void) pop(&dirp, sizeof(DIR *));
	       continue;
	  }
	  else 
	       continue;

     updir:
	  closedir(dirp);
	  string_pop(base);
	  if (retval) {
	       if (retval != STACK_EMPTY) {
		    error("pop");
		    (void) popall();
		    return retval;
	       }
	       return 0;
	  }
	  string_pop(rest);
	  string_pop(first);
	  retval = pop(&dirp, sizeof(DIR *));
	  if (retval) {
	       error("pop");
	       (void) popall();
	       return retval;
	  }
	  continue;
     }
}






/*
 * Function: do_recurs(char *name, int *num_found, char ***found,
 * 		       int options)
 *
 * Requires: name points to a NULL-terminated string, representing a
 *   filename pattern with regular filename characters, path
 *   separators and shell wildcard characters []*?; num_found points
 *   to a valid int memory storage location; found points to a valid
 *   char ** memory storage location.
 *
 * Effects: Returns a list of all the files in the file hierarchy that
 *   are underneath the specified file, governed by the options set in
 *   options.  Options are as described in the find_matches() description.
 *   RECURS_FIND_DELETED and RECURS_DELETED imply FIND_DELETED.
 *   RECURS_FIND_UNDELETED implies FIND_UNDELETED.
 *
 * Modifies: *num_found, *found.
 *
 * Algorithm:
 *
 * start:
 *   initialze found and num_found
 *   strcopy(base, name)
 *   dirp = opendir(base)
 *   check if we just opened a deleted symlink and return if we did
 *   check RECURS options and set FIND options as appropriate
 * 
 * loop:
 *   dp = readdir(dirp)
 *   if (! dp) goto updir
 *   is dp deleted?
 *     yes - is FIND_DELETED set?
 *             yes - add to list
 *                   is RECURS_DELETED set?
 *        	       yes - goto downdir
 *                     no - goto loop
 * 	       no - goto loop
 *     no - is FIND_UNDELETED set?
 *            yes - is the file a dotfile?
 * 		      yes - is FIND_DOTFILES set?
 * 			      yes - add to list
 * 			    goto loop
 * 		      no - add to list
 *                  are RECURS_FIND_DELETED and FIND_DELETED set?
 * 		      yes - goto downdir
 *                  is RECURS_FIND_UNDELETED set?
 * 		      yes - goto downdir
 * 		      no - goto loop
 * 	      no - goto loop
 *             
 * downdir:
 *   save dirp, base on stack
 *   base = dp->d_name appended to base
 *   try to open base -- opens?
 *     yes - is FOLLW_LINKS set?
 *             yes - is it deleted?
 * 		     yes - is it a link?
 * 			   yes - close the directory
 * 				 restore base and dirp
 * 				 goto loop
 *             no - is it a link?
 *                     yes - close the directory
 * 			     restore base and dirp
 * 			     goto loop
 *           is FOLLW_MOUNTPOINTS set?
 *             no - is it a mountpoint?
 *       	       yes - close the directory
 *                           restore base and dirp
 *                           goto loop
 *     no - is the error ENOTDIR?
 * 	      yes - don't worry about it
 * 	      no - report the error
 * 	    restore base and dirp
 *          goto loop
 * 
 * updir:
 *   close dirp
 *   restore base from stack
 *   STACK_EMPTY?
 *     yes - return from procedure with results
 *   restore dirp from stack
 *   goto loop
 */
int do_recurs(name, num_found, found, options)
char *name;
int *num_found;
char ***found;
int options;
{
     char base[MAXPATHLEN];
     struct direct *dp;
     DIR *dirp;
     int retval;
     int strsize;
     struct stat statbuf;
     int use_stat;
     
#ifdef DEBUG
     printf("de_recurs: opening %s\n", name);
#endif

     /* start: */
     
     *found = (char **) Malloc(0);
     if (! *found) {
	  set_error(errno);
	  error("Malloc");
	  return error_code;
     }
     *num_found = 0;
     strcpy(base, name);
     
     /* Never follow deleted symlinks */
     if (is_deleted(lastpart(base)) && is_link(base, (struct stat *) NULL)) {
	  return 0;
     }
     
     dirp = opendir(base);
     if (! dirp) {
	  /* If the problem is that it isn't a directory, just return */
	  /* with zero matches -- the file exists, but cannot be      */
	  /* recursively searched.  Otherwise, actually signal an     */
	  /* error. 						      */
	  if (errno != ENOTDIR) {
	       set_error(errno);
	       error(base);
	       return error_code;
	  }
	  else
	       return 0;
     }

     if (options & (RECURS_FIND_DELETED | RECURS_DELETED))
	  options |= FIND_DELETED;
     if (options & RECURS_FIND_UNDELETED)
	  options |= FIND_UNDELETED;
     
     while (1) {
	  dp = readdir(dirp);
	  if (! dp) goto updir;

	  if (is_deleted(dp->d_name)) {
	       if (options & FIND_DELETED) {
		    retval = add_str(found, *num_found,
				     append(base, dp->d_name));
		    if (retval) {
			 error("add_str");
			 (void) popall();
			 return retval;
		    }
		    (*num_found)++;
		    if (options & RECURS_DELETED)
			 goto downdir;
		    else
			 continue;
	       }
	       else
		    continue;
	  }

	  if (options & FIND_UNDELETED) {
	       if (is_dotfile(dp->d_name)) {
		    if (options & FIND_DOTFILES) {
			 retval = add_str(found, *num_found,
					  append(base, dp->d_name));
			 if (retval) {
			      error("add_str");
			      (void) popall();
			      return retval;
			 }
		    }
		    continue;
	       }
	       else {
		    retval = add_str(found, *num_found,
				     append(base, dp->d_name));
		    if (retval) {
			 error("add_str");
			 (void) popall();
			 return retval;
		    }
		    (*num_found)++;
	       }
	  }

	  if (! is_dotfile(dp->d_name)) {
	       if (options & RECURS_FIND_DELETED)
		    goto downdir;
	       if (options & RECURS_FIND_UNDELETED)
		    goto downdir;
	  }
	  
	  continue;
	  
	       
     downdir:
	  retval = push(&dirp, sizeof(DIR *));
	  if (retval) {
	       error("push");
	       (void) popall();
	       return retval;
	  }
	  string_push(base);
	  (void) strcpy(base, append(base, dp->d_name));

	  /*
	   * Originally, I did an opendir() right at the start and
	   * then only checked things if the opendir resulted in an
	   * error.  However, this is inefficient, because the
	   * opendir() procedure works by first calling open() on the
	   * file, and *then* calling fstat on the file descriptor
	   * that is returned.  since most of the time we will be
	   * trying to open things that are not directory, it is much
	   * more effecient to do the stat first here and to do the
	   * opendir only if the stat results are satisfactory.
	   */
	  use_stat = (options & FOLLW_LINKS) && (! is_deleted(lastpart(base)));
	  if (use_stat)
	       retval = stat(base, &statbuf);
	  else
	       retval = lstat(base, &statbuf);
	  if (retval == -1) {
	       set_error(errno);
	       error(base);
	       string_pop(base);
	       (void) pop(&dirp, sizeof(DIR *));
	       continue;
	  }
	  /* It's not a directory, so punt it and continue. */
	  if ((statbuf.st_mode & S_IFMT) != S_IFDIR) {
	       string_pop(base);
	       (void) pop(&dirp, sizeof(DIR *));
	       continue;
	  }

	  /* Actually try to open it. */
	  dirp = opendir(base);
	  if (! dirp) {
	       set_error(errno);
	       error(base);
	       string_pop(base);
	       (void) pop(&dirp, sizeof(DIR *));
	       continue;
	  }
	  
	  if (! (options & FOLLW_MOUNTPOINTS)) {
	       if (is_mountpoint(base, use_stat ? (struct stat *) NULL :
				 &statbuf)) {
		    closedir(dirp);
		    set_warning(PAT_IS_MOUNT);
		    error(base);
		    string_pop(base);
		    (void) pop(&dirp, sizeof(DIR *));
		    continue;
	       }
	  }
#ifdef DEBUG
	  printf("de_recurs: opening %s\n", base);
#endif
	  continue;
	  
     updir:
	  closedir(dirp);
	  string_pop(base);
	  if (retval) {
	       if (retval != STACK_EMPTY) {
		    error("pop");
		    (void) popall();
		    return retval;
	       }
	       return 0;
	  }
	  retval = pop(&dirp, sizeof(DIR *));
	  if (retval) {
	       error("pop");
	       (void) popall();
	       return retval;
	  }
	  continue;
     }
}


void free_list(list, num)
char **list;
int num;
{
     int i;

     for (i = 0; i < num; i++)
	  free(list[i]);

     free((char *) list);
}






/*
 * returns true if the filename has no globbing wildcards in it.  That
 * means no non-quoted question marks, asterisks, or open square
 * braces.  Assumes a null-terminated string, and a valid globbing
 */
int no_wildcards(name)
char *name;
{
     do {
	  switch (*name) {
	  case '\\':
	       name++;
	       break;
	  case '?':
	       return(0);
	  case '*':
	       return(0);
	  case '[':
	       return(0);
	  }
     } while (*++name);
     return(1);
}
