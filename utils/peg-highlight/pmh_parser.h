/* PEG Markdown Highlight
 * Copyright 2011-2016 Ali Rantakari -- http://hasseg.org
 * Licensed under the GPL2+ and MIT licenses (see LICENSE for more info).
 * 
 * pmh_parser.h
 */

#pragma GCC diagnostic ignored "-Wunused-parameter"

/** \file
* \brief Parser public interface.
*/

#ifndef __cplusplus
#include <stdbool.h>
#endif

#include <stdlib.h>
#include <assert.h>
#include "pmh_definitions.h"


/**
* \brief Parse Markdown text, return elements
* 
* Parses the given Markdown text and returns the results as an
* array of linked lists of elements, indexed by type.
* 
* \param[in]  text        The Markdown text to parse for highlighting.
* \param[in]  extensions  The extensions to use in parsing (a bitfield
*                         of pmh_extensions values).
* \param[out] out_result  A pmh_element array, indexed by type, containing
*                         the results of the parsing (linked lists of elements).
*                         You must pass this to pmh_free_elements() when it's
*                         not needed anymore.
* 
* \sa pmh_element_type
*/
void pmh_markdown_to_elements(char *text, int extensions,
                              pmh_element **out_result[]);

/**
* \brief Sort elements in list by start offset.
* 
* Sorts the linked lists of elements in the list returned by
* pmh_markdown_to_elements() by their start offsets (pos).
* 
* \param[in] element_lists  Array of linked lists of elements (output
*                           from pmh_markdown_to_elements()).
* 
* \sa pmh_markdown_to_elements
* \sa pmh_element::pos
*/
void pmh_sort_elements_by_pos(pmh_element *element_lists[]);

/**
* \brief Free pmh_element array
* 
* Frees an pmh_element array returned by pmh_markdown_to_elements().
* 
* \param[in]  elems  The pmh_element array resulting from calling
*                    pmh_markdown_to_elements().
* 
* \sa pmh_markdown_to_elements
*/
void pmh_free_elements(pmh_element **elems);

/**
* \brief Get element type name
* 
* \param[in]  type  The type value to get the name for.
* 
* \return The name of the given type as a null-terminated string.
* 
* \sa pmh_element_type
*/
char *pmh_element_name_from_type(pmh_element_type type);

/**
* \brief Get element type from a name
* 
* \param[in]  name  The name of the type.
* 
* \return The element type corresponding to the given name.
* 
* \sa pmh_element_type
*/
pmh_element_type pmh_element_type_from_name(char *name);

