/* PEG Markdown Highlight
 * Copyright 2011-2016 Ali Rantakari -- http://hasseg.org
 * Licensed under the GPL2+ and MIT licenses (see LICENSE for more info).
 * 
 * pmh_styleparser.h
 * 
 * Public interface of a parser for custom syntax highlighting stylesheets.
 */

/** \file
* \brief Style parser public interface.
*/

#include "pmh_definitions.h"
#include <stdbool.h>

/**
* \brief Color (ARGB) attribute value.
* 
* All values are 0-255.
*/
typedef struct
{
    int red;    /**< Red color component (0-255) */
    int green;  /**< Green color component (0-255) */
    int blue;   /**< Blue color component (0-255) */
    int alpha;  /**< Alpha (opacity) color component (0-255) */
} pmh_attr_argb_color;

/** \brief Font style attribute value. */
typedef struct
{
    bool italic;
    bool bold;
    bool underlined;
} pmh_attr_font_styles;

/** \brief Font size attribute value. */
typedef struct
{
    int size_pt;        /**< The font point size */
    bool is_relative;   /**< Whether the size is relative (i.e. size_pt points
                             larger than the default font) */
} pmh_attr_font_size;

/** \brief Style attribute types. */
typedef enum
{
    pmh_attr_type_foreground_color, /**< Foreground color */
    pmh_attr_type_background_color, /**< Background color */
    pmh_attr_type_caret_color,      /**< Caret (insertion point) color */
    pmh_attr_type_font_size_pt,     /**< Font size (in points) */
    pmh_attr_type_font_family,      /**< Font family */
    pmh_attr_type_font_style,       /**< Font style */
    pmh_attr_type_strike_color,     /**< Strike-through color */
    pmh_attr_type_other             /**< Arbitrary custom attribute */
} pmh_attr_type;

/**
* \brief Style attribute value.
* 
* Determine which member to access in this union based on the
* 'type' value of the pmh_style_attribute.
* 
* \sa pmh_style_attribute
*/
typedef union
{
    pmh_attr_argb_color *argb_color;    /**< ARGB color */
    pmh_attr_font_styles *font_styles;  /**< Font styles */
    pmh_attr_font_size *font_size;      /**< Font size */
    char *font_family;                  /**< Font family */
    char *string;                       /**< Arbitrary custom string value
                                             (use this if the attribute's type
                                             is pmh_attr_type_other) */
} pmh_attr_value;

/** \brief Style attribute. */
typedef struct pmh_style_attribute
{
    pmh_element_type lang_element_type; /**< The Markdown language element this
                                             style applies to */
    pmh_attr_type type;                 /**< The type of the attribute */
    char *name;                         /**< The name of the attribute (if type
                                             is pmh_attr_type_other, you can
                                             use this value to determine what
                                             the attribute is) */
    pmh_attr_value *value;              /**< The value of the attribute */
    struct pmh_style_attribute *next;   /**< Next attribute in linked list */
} pmh_style_attribute;

/** \brief Collection of styles. */
typedef struct
{
    /** Styles that apply to the editor in general */
    pmh_style_attribute *editor_styles;
    
    /** Styles that apply to the line in the editor where the caret (insertion
        point) resides */
    pmh_style_attribute *editor_current_line_styles;
    
    /** Styles that apply to the range of selected text in the editor */
    pmh_style_attribute *editor_selection_styles;
    
    /** Styles that apply to specific Markdown language elements */
    pmh_style_attribute **element_styles;
} pmh_style_collection;


/**
* \brief Parse stylesheet string, return style collection
* 
* \param[in] input                   The stylesheet string to parse.
* \param[in] error_callback          Callback function to be called when errors
*                                    occur during parsing. The first argument
*                                    to the callback function is the error
*                                    message and the second one the line number
*                                    in the original input where the error
*                                    occurred. The last argument will always
*                                    get the value you pass in for the
*                                    error_callback_context argument to this
*                                    function.
*                                    Pass in NULL to suppress error reporting.
* \param[in] error_callback_context  Arbitrary context pointer for the error
*                                    callback function; will be passed in as
*                                    the last argument to error_callback.
* 
* \return A pmh_style_collection. You must pass this value to
*         pmh_free_style_collection() when it's not needed anymore.
*/
pmh_style_collection *pmh_parse_styles(char *input,
                                       void(*error_callback)(char*,int,void*),
                                       void *error_callback_context);

/**
* \brief Free a pmh_style_collection.
* 
* Frees a pmh_style_collection value returned by pmh_parse_styles().
* 
* \param[in] collection  The collection to free.
*/
void pmh_free_style_collection(pmh_style_collection *collection);


char *pmh_attr_name_from_type(pmh_attr_type type);

pmh_attr_type pmh_attr_type_from_name(char *name);

