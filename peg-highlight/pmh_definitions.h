/* PEG Markdown Highlight
 * Copyright 2011-2016 Ali Rantakari -- http://hasseg.org
 * Licensed under the GPL2+ and MIT licenses (see LICENSE for more info).
 * 
 * pmh_definitions.h
 */

#ifndef pmh_MARKDOWN_DEFINITIONS
#define pmh_MARKDOWN_DEFINITIONS

/** \file
* \brief Global definitions for the parser.
*/


/**
* \brief Element types.
* 
* The first (documented) ones are language element types.
* 
* The last (non-documented) ones are utility types used
* by the parser itself.
* 
* \sa pmh_element
*/
typedef enum
{
    pmh_LINK,               /**< Explicit link */
    pmh_AUTO_LINK_URL,      /**< Implicit URL link */
    pmh_AUTO_LINK_EMAIL,    /**< Implicit email link */
    pmh_IMAGE,              /**< Image definition */
    pmh_CODE,               /**< Code (inline) */
    pmh_HTML,               /**< HTML */
    pmh_HTML_ENTITY,        /**< HTML special entity definition */
    pmh_EMPH,               /**< Emphasized text */
    pmh_STRONG,             /**< Strong text */
    pmh_LIST_BULLET,        /**< Bullet for an unordered list item */
    pmh_LIST_ENUMERATOR,    /**< Enumerator for an ordered list item */
    pmh_COMMENT,            /**< (HTML) Comment */
    
    // Code assumes that pmh_H1-6 are in order.
    pmh_H1,                 /**< Header, level 1 */
    pmh_H2,                 /**< Header, level 2 */
    pmh_H3,                 /**< Header, level 3 */
    pmh_H4,                 /**< Header, level 4 */
    pmh_H5,                 /**< Header, level 5 */
    pmh_H6,                 /**< Header, level 6 */
    
    pmh_BLOCKQUOTE,         /**< Blockquote */
    pmh_VERBATIM,           /**< Verbatim (e.g. block of code) */
    pmh_HTMLBLOCK,          /**< Block of HTML */
    pmh_HRULE,              /**< Horizontal rule */
    pmh_REFERENCE,          /**< Reference */
    pmh_NOTE,               /**< Note */
    pmh_STRIKE,             /**< Strike-through */
    
    // Utility types used by the parser itself:
    
    // List of pmh_RAW element lists, each to be processed separately from
    // others (for each element in linked lists of this type, `children` points
    // to a linked list of pmh_RAW elements):
    pmh_RAW_LIST,   /**< Internal to parser. Please ignore. */
    
    // Span marker for positions in original input to be post-processed
    // in a second parsing step:
    pmh_RAW,        /**< Internal to parser. Please ignore. */
    
    // Additional text to be parsed along with spans in the original input
    // (these may be added to linked lists of pmh_RAW elements):
    pmh_EXTRA_TEXT, /**< Internal to parser. Please ignore. */
    
    // Separates linked lists of pmh_RAW elements into parts to be processed
    // separate from each other:
    pmh_SEPARATOR,  /**< Internal to parser. Please ignore. */
    
    // Placeholder element used while parsing:
    pmh_NO_TYPE,    /**< Internal to parser. Please ignore. */
    
    // Linked list of *all* elements created while parsing:
    pmh_ALL         /**< Internal to parser. Please ignore. */
} pmh_element_type;

/**
* \brief Number of types in pmh_element_type.
* \sa pmh_element_type
*/
#define pmh_NUM_TYPES 31

/**
* \brief Number of *language element* types in pmh_element_type.
* \sa pmh_element_type
*/
#define pmh_NUM_LANG_TYPES (pmh_NUM_TYPES - 6)


/**
* \brief A Language element occurrence.
*/
struct pmh_Element
{
    pmh_element_type type;    /**< \brief Type of element */
    unsigned long pos;        /**< \brief Unicode code point offset marking the
                                          beginning of this element in the
                                          input. */
    unsigned long end;        /**< \brief Unicode code point offset marking the
                                          end of this element in the input. */
    struct pmh_Element *next; /**< \brief Next element in list */
    char *label;              /**< \brief Label (for links and references) */
    char *address;            /**< \brief Address (for links and references) */
};
typedef struct pmh_Element pmh_element;

/**
* \brief Bitfield enumeration of supported Markdown extensions.
*/
enum pmh_extensions
{
    pmh_EXT_NONE    = 0,        /**< No extensions */
    pmh_EXT_NOTES   = (1 << 0), /**< Footnote syntax:
                                     http://pandoc.org/README.html#footnotes */
    pmh_EXT_STRIKE  = (1 << 1)  /**< Strike-through syntax:
                                     http://pandoc.org/README.html#strikeout */
};

#endif
