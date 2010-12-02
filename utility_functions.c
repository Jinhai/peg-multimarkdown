/* utility_functions.c - List manipulation functions, element
 * constructors, and macro definitions for leg markdown parser. */

extern int strcasecmp(const char *string1, const char *string2);

static char *label_from_string(char *str, bool obfuscate) ;

/**********************************************************************

  List manipulation functions

 ***********************************************************************/

/* cons - cons an element onto a list, returning pointer to new head */
static element * cons(element *new, element *list) {
    assert(new != NULL);
    new->next = list;
    return new;
}

/* reverse - reverse a list, returning pointer to new list */
static element *reverse(element *list) {
    element *new = NULL;
    element *next = NULL;
    while (list != NULL) {
        next = list->next;
        new = cons(list, new);
        list = next;
    }
    return new;
}

/* concat_string_list - concatenates string contents of list of STR elements.
 * Frees STR elements as they are added to the concatenation. */
static GString *concat_string_list(element *list) {
    GString *result;
    element *next;
    result = g_string_new("");
    while (list != NULL) {
        assert(list->key == STR);
        assert(list->contents.str != NULL);
        g_string_append(result, list->contents.str);
        next = list->next;
        free_element(list);
        list = next;
    }
    return result;
}

/**********************************************************************

  Global variables used in parsing

 ***********************************************************************/

static char *charbuf = "";     /* Buffer of characters to be parsed. */
static element *references = NULL;    /* List of link references found. */
static element *notes = NULL;         /* List of footnotes found. */
static element *parse_result;  /* Results of parse. */
int syntax_extensions;  /* Syntax extensions selected. */

static element *labels = NULL;      /* List of labels found in document. */

/**********************************************************************

  Auxiliary functions for parsing actions.
  These make it easier to build up data structures (including lists)
  in the parsing actions.

 ***********************************************************************/

/* mk_element - generic constructor for element */
static element * mk_element(int key) {
    element *result = malloc(sizeof(element));
    result->key = key;
    result->children = NULL;
    result->next = NULL;
    result->contents.str = NULL;
    return result;
}

/* mk_str - constructor for STR element */
static element * mk_str(char *string) {
    element *result;
    assert(string != NULL);
    result = mk_element(STR);
    result->contents.str = strdup(string);
    return result;
}

/* mk_str_from_list - makes STR element by concatenating a
 * reversed list of strings, adding optional extra newline */
static element * mk_str_from_list(element *list, bool extra_newline) {
    element *result;
    GString *c = concat_string_list(reverse(list));
    if (extra_newline)
        g_string_append(c, "\n");
    result = mk_element(STR);
    result->contents.str = c->str;
    g_string_free(c, false);
    return result;
}

/* mk_list - makes new list with key 'key' and children the reverse of 'lst'.
 * This is designed to be used with cons to build lists in a parser action.
 * The reversing is necessary because cons adds to the head of a list. */
static element * mk_list(int key, element *lst) {
    element *result;
    result = mk_element(key);
    result->children = reverse(lst);
    return result;
}

/* mk_link - constructor for LINK element */
static element * mk_link(element *label, char *url, char *title) {
    element *result;
    result = mk_element(LINK);
    result->contents.link = malloc(sizeof(link));
    result->contents.link->label = label;
    result->contents.link->url = strdup(url);
    result->contents.link->title = strdup(title);
    return result;
}

/* extension = returns true if extension is selected */
static bool extension(int ext) {
    return (syntax_extensions & ext);
}

/* match_inlines - returns true if inline lists match (case-insensitive...) */
static bool match_inlines(element *l1, element *l2) {
    while (l1 != NULL && l2 != NULL) {
        if (l1->key != l2->key)
            return false;
        switch (l1->key) {
        case SPACE:
        case LINEBREAK:
        case ELLIPSIS:
        case EMDASH:
        case ENDASH:
        case APOSTROPHE:
            break;
        case CODE:
        case STR:
        case HTML:
            if (strcasecmp(l1->contents.str, l2->contents.str) == 0)
                break;
            else
                return false;
        case EMPH:
        case STRONG:
        case LIST:
        case SINGLEQUOTED:
        case DOUBLEQUOTED:
            if (match_inlines(l1->children, l2->children))
                break;
            else
                return false;
        case LINK:
        case IMAGE:
            return false;  /* No links or images within links */
        default:
            fprintf(stderr, "match_inlines encountered unknown key = %d\n", l1->key);
            exit(EXIT_FAILURE);
            break;
        }
        l1 = l1->next;
        l2 = l2->next;
    }
    return (l1 == NULL && l2 == NULL);  /* return true if both lists exhausted */
}

/* find_reference - return true if link found in references matching label.
 * 'link' is modified with the matching url and title. */
static bool find_reference(link *result, element *label) {
    element *cur = references;  /* pointer to walk up list of references */
    link *curitem;
    while (cur != NULL) {
        curitem = cur->contents.link;
        if (match_inlines(label, curitem->label)) {
            *result = *curitem;
            return true;
        }
        else
            cur = cur->next;
    }
    return false;
}

/* find_note - return true if note found in notes matching label.
if found, 'result' is set to point to matched note. */

static bool find_note(element **result, char *label) {
   element *cur = notes;  /* pointer to walk up list of notes */
   while (cur != NULL) {
       if (strcmp(label, cur->contents.str) == 0) {
           *result = cur;
           return true;
       }
       else
           cur = cur->next;
   }
   return false;
}



/**********************************************************************

  Definitions for leg parser generator.
  YY_INPUT is the function the parser calls to get new input.
  We take all new input from (static) charbuf.

 ***********************************************************************/

# define YYSTYPE element *
#ifdef __DEBUG__
# define YY_DEBUG 1
#endif

#define YY_INPUT(buf, result, max_size)              \
{                                                    \
    int yyc;                                         \
    if (charbuf && *charbuf != '\0') {               \
        yyc= *charbuf++;                             \
    } else {                                         \
        yyc= EOF;                                    \
    }                                                \
    result= (EOF == yyc) ? 0 : (*(buf)= yyc, 1);     \
}


/* peg-multimarkdown additions */

/* print_raw_element - print an element as original text */
static void print_raw_element(GString *out, element *elt) {
    g_string_append_printf(out, "%s", elt->contents.str);
}

/* print_raw_element_list - print a list of elements as original text */
static void print_raw_element_list(GString *out, element *list) {
    while (list != NULL) {
        print_raw_element(out, list);
        list = list->next;
    }
}

/* label_from_element_list */

static char *label_from_element_list(element *list, bool obfuscate) {
    char *label;
    GString *raw = g_string_new("");
    print_raw_element_list(raw, list);
    label =  label_from_string(raw->str,obfuscate);
    return label;
}

/* label_from_string - strip spaces and illegal characters to generate valid 
    HTML id */
static char *label_from_string(char *str, bool obfuscate) {
    bool valid = FALSE;
    GString *out = g_string_new("");

	if ( strcspn(str,"[") == strlen(str)) {
	    while (*str != '\0') {
	        if (valid) {
	        /* can relax on following characters */
	            if ((*str >= '0' && *str <= '9') || (*str >= 'A' && *str <= 'Z')
	                || (*str >= 'a' && *str <= 'z') || (*str == '.') || (*str== '_')
	                || (*str== '-') || (*str== ':'))
	            {
	                g_string_append_c(out, tolower(*str));
	            }           
	        } else {
	        /* need alpha as first character */
	            if ((*str >= 'A' && *str <= 'Z') || (*str >= 'a' && *str <= 'z'))
	            {
	                g_string_append_c(out, tolower(*str));
	                valid = TRUE;
	            }
	        }
	    str++;
	    }
	} else {
		char *token;
		token = strtok (&str[strcspn(str,"[")+1],"]");
		g_string_append_c(out, label_from_string(token,obfuscate));
	}
    return out->str;
}

/* find_label - return true if header, table, etc is found matching label.
 * 'link' is modified with the matching url and title. */
static bool find_label(link *result, element *label) {
    element *cur = labels;  /* pointer to walk up list of references */
    GString *text = g_string_new("");
    print_raw_element_list(text, label);
    GString *query = g_string_new(label_from_string(text->str,0));
    g_string_free(text, true);

    while (cur != NULL) {
        if (strcmp(query->str,cur->contents.str) == 0) {
            g_string_free(query, true);
            return true;
        }
        else
           cur = cur->next;
    }
    g_string_free(query, true);
    return false;
}
