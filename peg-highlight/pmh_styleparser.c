/* PEG Markdown Highlight
 * Copyright 2011-2016 Ali Rantakari -- http://hasseg.org
 * Licensed under the GPL2+ and MIT licenses (see LICENSE for more info).
 * 
 * styleparser.c
 * 
 * Parser for custom syntax highlighting stylesheets.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

#include "pmh_styleparser.h"
#include "pmh_parser.h"


#if pmh_DEBUG_OUTPUT
#define pmhsp_PRINTF(x, ...) fprintf(stderr, x, ##__VA_ARGS__)
#else
#define pmhsp_PRINTF(x, ...)
#endif


// vasprintf is not in the C standard nor in POSIX so we provide our own
static int our_vasprintf(char **strptr, const char *fmt, va_list argptr)
{
    int ret;
    va_list argptr2;
    *strptr = NULL;
    
    va_copy(argptr2, argptr);
    ret = vsnprintf(NULL, 0, fmt, argptr2);
    if (ret <= 0)
        return ret;
    
    *strptr = (char *)malloc(ret+1);
    if (*strptr == NULL)
        return -1;
    
    va_copy(argptr2, argptr);
    ret = vsnprintf(*strptr, ret+1, fmt, argptr2);
    
    return ret;
}



// Parsing context data
typedef struct
{
    char *input;
    void (*error_callback)(char*,int,void*);
    void *error_callback_context;
    int styles_pos;
    pmh_style_collection *styles;
} style_parser_data;

typedef struct raw_attribute
{
    char *name;
    char *value;
    int line_number;
    struct raw_attribute *next;
} raw_attribute;

static raw_attribute *new_raw_attribute(char *name, char *value,
                                        int line_number)
{
    raw_attribute *v = (raw_attribute *)malloc(sizeof(raw_attribute));
    v->name = name;
    v->value = value;
    v->line_number = line_number;
    v->next = NULL;
    return v;
}

static void free_raw_attributes(raw_attribute *list)
{
    raw_attribute *cur = list;
    while (cur != NULL)
    {
        if (cur->name != NULL) free(cur->name);
        if (cur->value != NULL) free(cur->value);
        raw_attribute *this = cur;
        cur = cur->next;
        free(this);
    }
}


static void report_error(style_parser_data *p_data,
                         int line_number, char *str, ...)
{
    if (p_data->error_callback == NULL)
        return;
    va_list argptr;
    va_start(argptr, str);
    char *errmsg;
    our_vasprintf(&errmsg, str, argptr);
    va_end(argptr);
    p_data->error_callback(errmsg, line_number,
                           p_data->error_callback_context);
    free(errmsg);
}



static char *trim_str(char *str)
{
    while (isspace(*str))
        str++;
    if (*str == '\0')
        return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace(*end))
        end--;
    *(end+1) = '\0';
    return str;
}

static char *trim_str_dup(char *str)
{
    size_t start = 0;
    while (isspace(*(str + start)))
        start++;
    size_t end = strlen(str) - 1;
    while (start < end && isspace(*(str + end)))
        end--;
    
    size_t len = end - start + 1;
    char *ret = (char *)malloc(sizeof(char)*len + 1);
    *ret = '\0';
    strncat(ret, (str + start), len);
    
    return ret;
}

static char *strcpy_lower(char *str)
{
    char *low = strdup(str);
    int i;
    int len = strlen(str);
    for (i = 0; i < len; i++)
        *(low+i) = tolower(*(low+i));
    return low;
}

static char *standardize_str(char *str)
{
    return strcpy_lower(trim_str(str));
}




static pmh_attr_argb_color *new_argb_color(int r, int g, int b, int a)
{
    pmh_attr_argb_color *c = (pmh_attr_argb_color *)
                             malloc(sizeof(pmh_attr_argb_color));
    c->red = r; c->green = g; c->blue = b; c->alpha = a;
    return c;
}
static pmh_attr_argb_color *new_argb_from_hex(long long hex, bool has_alpha)
{
    // 0xaarrggbb
    int a = has_alpha ? ((hex >> 24) & 0xFF) : 255;
    int r = ((hex >> 16) & 0xFF);
    int g = ((hex >> 8) & 0xFF);
    int b = (hex & 0xFF);
    return new_argb_color(r,g,b,a);
}
static pmh_attr_argb_color *new_argb_from_hex_str(style_parser_data *p_data,
                                                  int attr_line_number,
                                                  char *str)
{
    // "aarrggbb"
    int len = strlen(str);
    if (len != 6 && len != 8) {
        report_error(p_data, attr_line_number,
                     "Value '%s' is not a valid color value: it should be a "
                     "hexadecimal number, 6 or 8 characters long.",
                     str);
        return NULL;
    }
    char *endptr = NULL;
    long long num = strtoll(str, &endptr, 16);
    if (*endptr != '\0') {
        report_error(p_data, attr_line_number,
                     "Value '%s' is not a valid color value: the character "
                     "'%c' is invalid. The color value should be a hexadecimal "
                     "number, 6 or 8 characters long.",
                     str, *endptr);
        return NULL;
    }
    return new_argb_from_hex(num, (len == 8));
}

static pmh_attr_value *new_attr_value()
{
    return (pmh_attr_value *)malloc(sizeof(pmh_attr_value));
}

static pmh_attr_font_styles *new_font_styles()
{
    pmh_attr_font_styles *ret = (pmh_attr_font_styles *)
                                malloc(sizeof(pmh_attr_font_styles));
    ret->italic = false;
    ret->bold = false;
    ret->underlined = false;
    return ret;
}

static pmh_attr_font_size *new_font_size()
{
    pmh_attr_font_size *ret = (pmh_attr_font_size *)
                              malloc(sizeof(pmh_attr_font_size));
    ret->is_relative = false;
    ret->size_pt = 0;
    return ret;
}

static pmh_style_attribute *new_attr(char *name, pmh_attr_type type)
{
    pmh_style_attribute *attr = (pmh_style_attribute *)malloc(sizeof(pmh_style_attribute));
    attr->name = strdup(name);
    attr->type = type;
    attr->next = NULL;
    return attr;
}

static void free_style_attributes(pmh_style_attribute *list)
{
    pmh_style_attribute *cur = list;
    while (cur != NULL)
    {
        if (cur->name != NULL)
            free(cur->name);
        if (cur->value != NULL)
        {
            if (cur->type == pmh_attr_type_foreground_color
                || cur->type == pmh_attr_type_background_color
                || cur->type == pmh_attr_type_caret_color
                || cur->type == pmh_attr_type_strike_color)
                free(cur->value->argb_color);
            else if (cur->type == pmh_attr_type_font_family)
                free(cur->value->font_family);
            else if (cur->type == pmh_attr_type_font_style)
                free(cur->value->font_styles);
            else if (cur->type == pmh_attr_type_font_size_pt)
                free(cur->value->font_size);
            else if (cur->type == pmh_attr_type_other)
                free(cur->value->string);
            free(cur->value);
        }
        pmh_style_attribute *this = cur;
        cur = cur->next;
        free(this);
    }
}





#define IF_ATTR_NAME(x) if (strcmp(x, name) == 0)
pmh_attr_type pmh_attr_type_from_name(char *name)
{
    IF_ATTR_NAME("color") return pmh_attr_type_foreground_color;
    else IF_ATTR_NAME("foreground") return pmh_attr_type_foreground_color;
    else IF_ATTR_NAME("foreground-color") return pmh_attr_type_foreground_color;
    else IF_ATTR_NAME("background") return pmh_attr_type_background_color;
    else IF_ATTR_NAME("background-color") return pmh_attr_type_background_color;
    else IF_ATTR_NAME("caret") return pmh_attr_type_caret_color;
    else IF_ATTR_NAME("caret-color") return pmh_attr_type_caret_color;
    else IF_ATTR_NAME("strike") return pmh_attr_type_strike_color;
    else IF_ATTR_NAME("strike-color") return pmh_attr_type_strike_color;
    else IF_ATTR_NAME("font-size") return pmh_attr_type_font_size_pt;
    else IF_ATTR_NAME("font-family") return pmh_attr_type_font_family;
    else IF_ATTR_NAME("font-style") return pmh_attr_type_font_style;
    return pmh_attr_type_other;
}

char *pmh_attr_name_from_type(pmh_attr_type type)
{
    switch (type)
    {
        case pmh_attr_type_foreground_color:
            return "foreground-color"; break;
        case pmh_attr_type_background_color:
            return "background-color"; break;
        case pmh_attr_type_caret_color:
            return "caret-color"; break;
        case pmh_attr_type_strike_color:
            return "strike-color"; break;
        case pmh_attr_type_font_size_pt:
            return "font-size"; break;
        case pmh_attr_type_font_family:
            return "font-family"; break;
        case pmh_attr_type_font_style:
            return "font-style"; break;
        default:
            return "unknown";
    }
}


typedef struct multi_value
{
    char *value;
    size_t length;
    int line_number;
    struct multi_value *next;
} multi_value;

static multi_value *split_multi_value(char *input, char separator)
{
    multi_value *head = NULL;
    multi_value *tail = NULL;
    
    char *c = input;
    while (*c != '\0')
    {
        size_t i;
        for (i = 0; (*(c+i) != '\0' && *(c+i) != separator); i++);
        
        multi_value *mv = (multi_value *)malloc(sizeof(multi_value));
        mv->value = (char *)malloc(sizeof(char)*i + 1);
        mv->length = i;
        mv->line_number = 0;
        mv->next = NULL;
        *mv->value = '\0';
        strncat(mv->value, c, i);
        
        if (head == NULL) {
            head = mv;
            tail = mv;
        } else {
            tail->next = mv;
            tail = mv;
        }
        
        if (*(c+i) == separator)
            i++;
        c += i;
    }
    
    return head;
}

static void free_multi_value(multi_value *val)
{
    multi_value *cur = val;
    while (cur != NULL)
    {
        multi_value *this = cur;
        multi_value *next_cur = cur->next;
        free(this->value);
        free(this);
        cur = next_cur;
    }
}




#define EQUALS(a,b) (strcmp(a, b) == 0)

static pmh_style_attribute *interpret_attributes(style_parser_data *p_data,
                                                 pmh_element_type lang_element_type,
                                                 raw_attribute *raw_attributes)
{
    pmh_style_attribute *attrs = NULL;
    
    raw_attribute *cur = raw_attributes;
    while (cur != NULL)
    {
        pmh_attr_type atype = pmh_attr_type_from_name(cur->name);
        pmh_style_attribute *attr = new_attr(cur->name, atype);
        attr->lang_element_type = lang_element_type;
        attr->value = new_attr_value();
        
        if (atype == pmh_attr_type_foreground_color
            || atype == pmh_attr_type_background_color
            || atype == pmh_attr_type_caret_color
            || atype == pmh_attr_type_strike_color)
        {
            char *hexstr = trim_str(cur->value);
            // new_argb_from_hex_str() reports conversion errors
            attr->value->argb_color =
                new_argb_from_hex_str(p_data, cur->line_number, hexstr);
            if (attr->value->argb_color == NULL) {
                free_style_attributes(attr);
                attr = NULL;
            }
        }
        else if (atype == pmh_attr_type_font_size_pt)
        {
            pmh_attr_font_size *fs = new_font_size();
            attr->value->font_size = fs;
            
            char *trimmed_value = trim_str_dup(cur->value);
            
            fs->is_relative = (*trimmed_value == '+' || *trimmed_value == '-');
            char *endptr = NULL;
            fs->size_pt = (int)strtol(cur->value, &endptr, 10);
            if (endptr == cur->value) {
                report_error(p_data, cur->line_number,
                             "Value '%s' is invalid for attribute '%s'",
                             cur->value, cur->name);
                free_style_attributes(attr);
                attr = NULL;
            }
            
            free(trimmed_value);
        }
        else if (atype == pmh_attr_type_font_family)
        {
            attr->value->font_family = trim_str_dup(cur->value);
        }
        else if (atype == pmh_attr_type_font_style)
        {
            attr->value->font_styles = new_font_styles();
            multi_value *values = split_multi_value(cur->value, ',');
            multi_value *value_cur = values;
            while (value_cur != NULL)
            {
                char *standardized_value = standardize_str(value_cur->value);
                
                if (EQUALS(standardized_value, "italic"))
                    attr->value->font_styles->italic = true;
                else if (EQUALS(standardized_value, "bold"))
                    attr->value->font_styles->bold = true;
                else if (EQUALS(standardized_value, "underlined"))
                    attr->value->font_styles->underlined = true;
                else {
                    report_error(p_data, cur->line_number,
                                 "Value '%s' is invalid for attribute '%s'",
                                 standardized_value, cur->name);
                }
                
                free(standardized_value);
                value_cur = value_cur->next;
            }
            free_multi_value(values);
        }
        else if (atype == pmh_attr_type_other)
        {
            attr->value->string = trim_str_dup(cur->value);
        }
        
        if (attr != NULL) {
            // add to linked list
            attr->next = attrs;
            attrs = attr;
        }
        
        cur = cur->next;
    }
    
    return attrs;
}


static void interpret_and_add_style(style_parser_data *p_data,
                                    char *style_rule_name,
                                    int style_rule_line_number,
                                    raw_attribute *raw_attributes)
{
    bool isEditorType = false;
    bool isCurrentLineType = false;
    bool isSelectionType = false;
    pmh_element_type type = pmh_element_type_from_name(style_rule_name);
    if (type == pmh_NO_TYPE)
    {
        if (EQUALS(style_rule_name, "editor"))
            isEditorType = true, type = pmh_NO_TYPE;
        else if (EQUALS(style_rule_name, "editor-current-line"))
            isCurrentLineType = true, type = pmh_NO_TYPE;
        else if (EQUALS(style_rule_name, "editor-selection"))
            isSelectionType = true, type = pmh_NO_TYPE;
        else {
            report_error(p_data, style_rule_line_number,
                "Style rule '%s' is not a language element type name or "
                "one of the following: 'editor', 'editor-current-line', "
                "'editor-selection'",
                style_rule_name);
            return;
        }
    }
    pmh_style_attribute *attrs = interpret_attributes(p_data, type, raw_attributes);
    if (isEditorType)
        p_data->styles->editor_styles = attrs;
    else if (isCurrentLineType)
        p_data->styles->editor_current_line_styles = attrs;
    else if (isSelectionType)
        p_data->styles->editor_selection_styles = attrs;
    else
        p_data->styles->element_styles[(p_data->styles_pos)++] = attrs;
}







static bool char_is_whitespace(char c)
{
    return (c == ' ' || c == '\t');
}

static bool char_begins_linecomment(char c)
{
    return (c == '#');
}

static bool line_is_comment(multi_value *line)
{
    char *c;
    for (c = line->value; *c != '\0'; c++)
    {
        if (!char_is_whitespace(*c))
            return char_begins_linecomment(*c);
    }
    return false;
}

static bool line_is_empty(multi_value *line)
{
    char *c;
    for (c = line->value; *c != '\0'; c++)
    {
        if (!char_is_whitespace(*c))
            return false;
    }
    return true;
}



typedef struct block
{
    multi_value *lines;
    struct block *next;
} block;

static block *new_block()
{
    block *ret = (block *)malloc(sizeof(block));
    ret->next = NULL;
    ret->lines = NULL;
    return ret;
}

static void free_blocks(block *val)
{
    block *cur = val;
    while (cur != NULL)
    {
        block *this = cur;
        block *next = this->next;
        free_multi_value(this->lines);
        free(this);
        cur = next;
    }
}

static block *get_blocks(char *input)
{
    block *head = NULL;
    block *tail = NULL;
    block *current_block = NULL;
    
    multi_value *discarded_lines = NULL;
    
    int line_number_counter = 1;
    
    multi_value *lines = split_multi_value(input, '\n');
    multi_value *previous_line = NULL;
    multi_value *line_cur = lines;
    while (line_cur != NULL)
    {
        bool discard_line = false;
        
        line_cur->line_number = line_number_counter++;
        
        if (line_is_empty(line_cur))
        {
            discard_line = true;
            
            if (current_block != NULL)
            {
                // terminate block
                if (tail != current_block)
                    tail->next = current_block;
                tail = current_block;
                current_block = NULL;
                previous_line->next = NULL;
            }
        }
        else if (line_is_comment(line_cur))
        {
            // Do not discard (i.e. free()) comment lines within blocks:
            if (current_block == NULL)
                discard_line = true;
        }
        else
        {
            if (current_block == NULL)
            {
                // start block
                current_block = new_block();
                current_block->lines = line_cur;
                if (previous_line != NULL)
                    previous_line->next = NULL;
            }
            if (head == NULL) {
                head = current_block;
                tail = current_block;
            }
        }
        
        multi_value *next_cur = line_cur->next;
        previous_line = (discard_line) ? NULL : line_cur;
        
        if (discard_line) {
            line_cur->next = discarded_lines;
            discarded_lines = line_cur;
        }
        
        line_cur = next_cur;
    }
    
    if (current_block != NULL && tail != current_block)
        tail->next = current_block;
    
    free_multi_value(discarded_lines);
    
    return head;
}


#define ASSIGNMENT_OP_UITEXT        "':' or '='"
#define IS_ASSIGNMENT_OP(c)         ((c) == ':' || (c) == '=')
#define IS_STYLE_RULE_NAME_CHAR(c)  \
    ( (c) != '\0' && !isspace(c) \
      && !char_begins_linecomment(c) && !IS_ASSIGNMENT_OP(c) )
#define IS_ATTRIBUTE_NAME_CHAR(c)  \
    ( (c) != '\0' && !char_begins_linecomment(c) && !IS_ASSIGNMENT_OP(c) )
#define IS_ATTRIBUTE_VALUE_CHAR(c)  \
    ( (c) != '\0' && !char_begins_linecomment(c) )

static char *get_style_rule_name(multi_value *line)
{
    char *str = line->value;
    
    // Scan past leading whitespace:
    size_t start_index;
    for (start_index = 0;
         (*(str+start_index) != '\0' && isspace(*(str+start_index)));
         start_index++);
    
    // Scan until style rule name characters end:
    size_t value_end_index;
    for (value_end_index = start_index;
         IS_STYLE_RULE_NAME_CHAR(*(str + value_end_index));
         value_end_index++);
    
    // Copy style rule name:
    size_t value_len = value_end_index - start_index;
    char *value = (char *)malloc(sizeof(char)*value_len + 1);
    *value = '\0';
    strncat(value, (str + start_index), value_len);
    
    return value;
}

static bool parse_attribute_line(style_parser_data *p_data, multi_value *line,
                                 char **out_attr_name, char **out_attr_value)
{
    char *str = line->value;
    
    // Scan past leading whitespace:
    size_t name_start_index;
    for (name_start_index = 0;
         ( *(str+name_start_index) != '\0' &&
           isspace(*(str+name_start_index)) );
         name_start_index++);
    
    // Scan until attribute name characters end:
    size_t name_end_index;
    for (name_end_index = name_start_index;
         IS_ATTRIBUTE_NAME_CHAR(*(str + name_end_index));
         name_end_index++);
    // Scan backwards to trim trailing whitespace off:
    while (name_start_index < name_end_index
           && isspace(*(str + name_end_index - 1)))
        name_end_index--;
    
    // Scan until just after the first assignment operator:
    size_t assignment_end_index;
    for (assignment_end_index = name_end_index;
         ( *(str + assignment_end_index) != '\0' &&
           !IS_ASSIGNMENT_OP(*(str + assignment_end_index)) );
         assignment_end_index++);
    
    // Scan over the found assignment operator, or report error:
    if (IS_ASSIGNMENT_OP(*(str + assignment_end_index)))
        assignment_end_index++;
    else
    {
        report_error(p_data, line->line_number,
                     "Invalid attribute definition: str does not contain "
                     "an assignment operator (%s): '%s'",
                     ASSIGNMENT_OP_UITEXT, str);
        return false;
    }
    
    size_t value_start_index = assignment_end_index;
    // Scan until attribute value characters end:
    size_t value_end_index;
    for (value_end_index = value_start_index;
         IS_ATTRIBUTE_VALUE_CHAR(*(str + value_end_index));
         value_end_index++);
    
    // Copy attribute name:
    size_t name_len = name_end_index - name_start_index;
    char *attr_name = (char *)malloc(sizeof(char)*name_len + 1);
    *attr_name = '\0';
    strncat(attr_name, (str + name_start_index), name_len);
    *out_attr_name = attr_name;
    
    // Copy attribute value:
    size_t attr_value_len = value_end_index - assignment_end_index;
    char *attr_value_str = (char *)malloc(sizeof(char)*attr_value_len + 1);
    *attr_value_str = '\0';
    strncat(attr_value_str, (str + assignment_end_index), attr_value_len);
    *out_attr_value = attr_value_str;
    
    return true;
}


#define HAS_UTF8_BOM(x)         ( ((*x & 0xFF) == 0xEF)\
                                  && ((*(x+1) & 0xFF) == 0xBB)\
                                  && ((*(x+2) & 0xFF) == 0xBF) )

// - Removes UTF-8 BOM
// - Standardizes line endings to \n
static char *strcpy_preformat_style(char *str)
{
    char *new_str = (char *)malloc(sizeof(char) * strlen(str) + 1);
    
    char *c = str;
    int i = 0;
    
    if (HAS_UTF8_BOM(c))
        c += 3;
    
    while (*c != '\0')
    {
        if (*c == '\r' && *(c+1) == '\n')
        {
            *(new_str+i) = '\n';
            i++;
            c += 2;
        }
        else if (*c == '\r')
        {
            *(new_str+i) = '\n';
            i++;
            c++;
        }
        else
        {
            *(new_str+i) = *c;
            i++;
            c++;
        }
    }
    *(new_str+i) = '\0';
    
    return new_str;
}



static void _sty_parse(style_parser_data *p_data)
{
    // We don't have to worry about leaking the original p_data->input;
    // the user of the library is responsible for that:
    p_data->input = strcpy_preformat_style(p_data->input);
    
    block *blocks = get_blocks(p_data->input);
    
    block *block_cur = blocks;
    while (block_cur != NULL)
    {
        pmhsp_PRINTF("Block:\n");
        multi_value *header_line = block_cur->lines;
        if (header_line == NULL) {
            block_cur = block_cur->next;
            continue;
        }
        
        pmhsp_PRINTF("  Head line (len %ld): '%s'\n",
                     header_line->length, header_line->value);
        char *style_rule_name = get_style_rule_name(header_line);
        pmhsp_PRINTF("  Style rule name: '%s'\n", style_rule_name);
        
        multi_value *attr_line_cur = header_line->next;
        if (attr_line_cur == NULL)
            report_error(p_data, header_line->line_number,
                         "No style attributes defined for style rule '%s'",
                         style_rule_name);
        
        raw_attribute *attributes_head = NULL;
        raw_attribute *attributes_tail = NULL;
        
        while (attr_line_cur != NULL)
        {
            if (line_is_comment(attr_line_cur))
            {
                attr_line_cur = attr_line_cur->next;
                continue;
            }
            
            pmhsp_PRINTF("  Attr line (len %ld): '%s'\n",
                         attr_line_cur->length, attr_line_cur->value);
            char *attr_name_str;
            char *attr_value_str;
            bool success = parse_attribute_line(p_data,
                                                attr_line_cur,
                                                &attr_name_str,
                                                &attr_value_str);
            if (success)
            {
                pmhsp_PRINTF("  Attr: '%s' Value: '%s'\n",
                             attr_name_str, attr_value_str);
                raw_attribute *attribute =
                    new_raw_attribute(attr_name_str, attr_value_str,
                                      attr_line_cur->line_number);
                if (attributes_head == NULL) {
                    attributes_head = attribute;
                    attributes_tail = attribute;
                } else {
                    attributes_tail->next = attribute;
                    attributes_tail = attribute;
                }
            }
            
            attr_line_cur = attr_line_cur->next;
        }
        
        if (attributes_head != NULL)
        {
            interpret_and_add_style(p_data, style_rule_name,
                                    header_line->line_number, attributes_head);
            free_raw_attributes(attributes_head);
        }
        
        free(style_rule_name);
        
        block_cur = block_cur->next;
    }
    
    free_blocks(blocks);
    free(p_data->input);
}



static pmh_style_collection *new_style_collection()
{
    pmh_style_collection *sc = (pmh_style_collection *)
                               malloc(sizeof(pmh_style_collection));
    
    sc->element_styles = (pmh_style_attribute**)
                         malloc(sizeof(pmh_style_attribute*)
                                * pmh_NUM_LANG_TYPES);
    int i;
    for (i = 0; i < pmh_NUM_LANG_TYPES; i++)
        sc->element_styles[i] = NULL;
    
    sc->editor_styles = NULL;
    sc->editor_current_line_styles = NULL;
    sc->editor_selection_styles = NULL;
    
    return sc;
}

void pmh_free_style_collection(pmh_style_collection *coll)
{
    free_style_attributes(coll->editor_styles);
    free_style_attributes(coll->editor_current_line_styles);
    free_style_attributes(coll->editor_selection_styles);
    int i;
    for (i = 0; i < pmh_NUM_LANG_TYPES; i++)
        free_style_attributes(coll->element_styles[i]);
    free(coll->element_styles);
    free(coll);
}

static style_parser_data *new_style_parser_data(char *input)
{
    style_parser_data *p_data = (style_parser_data*)
                                malloc(sizeof(style_parser_data));
    p_data->input = input;
    p_data->styles_pos = 0;
    p_data->styles = new_style_collection();
    return p_data;
}

pmh_style_collection *pmh_parse_styles(char *input,
                                       void(*error_callback)(char*,int,void*),
                                       void *error_callback_context)
{
    style_parser_data *p_data = new_style_parser_data(input);
    p_data->error_callback = error_callback;
    p_data->error_callback_context = error_callback_context;
    
    _sty_parse(p_data);
    
    pmh_style_collection *ret = p_data->styles;
    free(p_data);
    return ret;
}


