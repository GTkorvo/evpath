#include "config.h"

#include "io.h"
#include "atl.h"
#include "evpath.h"
#include "gen_thread.h"
#include "cm_internal.h"
#ifndef MODULE
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <malloc.h>

#else
#include "kernel/kcm.h"
#include "kernel/cm_kernel.h"
#include "kernel/library.h"
#include "assert.h"

#endif

static void add_format_to_cm ARGS((CManager cm, CMFormat format));
static void add_user_format_to_cm ARGS((CManager cm, CMFormat format));

CMFormat
CMlookup_format(cm, field_list)
CManager cm;
IOFieldList field_list;
{
    int i;
    for (i=0; i< cm->reg_format_count; i++) {
	if (cm->reg_formats[i]->field_list == field_list) {
	    return cm->reg_formats[i];
	}
    }
    return NULL;
}

IOFormat
CMlookup_user_format(cm, field_list)
CManager cm;
IOFieldList field_list;
{
    int i;
    for (i=0; i< cm->reg_user_format_count; i++) {
	if (cm->reg_user_formats[i]->field_list == field_list) {
	    return cm->reg_user_formats[i]->format;
	}
    }
    return NULL;
}

IOContext
CMget_user_type_context(cm)
CManager cm;
{
    return create_IOsubcontext(cm->IOcontext);
}

void
CMfree_user_type_context(cm, context)
CManager cm;
IOContext context;
{
    int i, j;
    CMFormat* tmp_user_formats;
    int new_count = 0;
    int dead_count = 0;

    CManager_lock(cm);

    for (i=0; i < cm->reg_user_format_count; i++) {
        if (cm->reg_user_formats[i]->IOsubcontext == context) {
          dead_count++;
        }
    }

    new_count = cm->reg_user_format_count - dead_count;
    tmp_user_formats = CMmalloc (sizeof (CMFormat) * new_count);
    
    for (i=0, j=0; i < cm->reg_user_format_count; i++) {
	if (cm->reg_user_formats[i]->IOsubcontext != context) {
	    tmp_user_formats[j++] = cm->reg_user_formats[i];
	} else {
	    CMfree(cm->reg_user_formats[i]->format_name);
	    CMfree(cm->reg_user_formats[i]);
	}
    }

    free_IOcontext (context);
    CMfree (cm->reg_user_formats);

    cm->reg_user_format_count = new_count;
    cm->reg_user_formats = tmp_user_formats;

    CManager_unlock (cm);
}

CMFormat
CMregister_format(cm, format_name, field_list, subformat_list)
CManager cm;
char *format_name;
IOFieldList field_list;
CMFormatList subformat_list;
{
    CMFormat format;

    if ((field_list == NULL) || (format_name == NULL) || (cm == NULL)) 
	return NULL;

    CManager_lock(cm);
    format = CMmalloc(sizeof(struct _CMFormat));
    
    format->cm = cm;
    format->format_name = CMmalloc(strlen(format_name) + 1);
    strcpy(format->format_name, format_name);
    /*  create a new subcontext (to localize name resolution) */
    format->IOsubcontext = create_IOsubcontext(cm->IOcontext);
    format->format = NULL;
    format->field_list_addr = field_list;
    format->handler = (CMHandlerFunc) NULL;
    format->client_data = NULL;
    format->field_list = field_list;
    format->subformat_list = subformat_list;
    format->opt_info = NULL;
    format->registration_pending = 1;

    add_format_to_cm(cm, format);
    CManager_unlock(cm);
    return format;
}

CMFormat
CMregister_opt_format(cm, format_name, field_list, subformat_list, opt_info)
CManager cm;
char *format_name;
IOFieldList field_list;
CMFormatList subformat_list;
IOOptInfo *opt_info;
{
    CMFormat format;
    format = CMregister_format(cm, format_name, field_list, subformat_list);
    if(format) {
	format->opt_info = opt_info;
    }
    return format;
}

void *CMcreate_compat_info(format, xform_code, len_p)
CMFormat format;
char *xform_code;
int *len_p;
{
    if(!format) return NULL;
    if(!format->format) CMcomplete_format_registration(format, 0);
    if(!format->format) return NULL;
    return create_compat_info(format->format, xform_code, len_p);
}

static int
CMregister_subformats(context, field_list, sub_list)
IOContext context;
IOFieldList field_list;
CMFormatList sub_list;
{
    char **subformats = get_subformat_names(field_list);
    char **save_subformats = subformats;

    if (subformats != NULL) {
	while (*subformats != NULL) {
	    int i = 0;
	    if (get_IOformat_by_name_IOcontext(context, *subformats) != NULL) {
		/* already registered this subformat */
		goto next_format;
	    }
	    while (sub_list && (sub_list[i].format_name != NULL)) {
		if (strcmp(sub_list[i].format_name, *subformats) == 0) {
		    IOFormat tmp;
		    if (CMregister_subformats(context, sub_list[i].field_list,
					      sub_list) != 1) {
			fprintf(stderr, "Format registration failed for subformat \"%s\"\n",
				sub_list[i].format_name);
			return 0;
		    }
		    tmp = register_IOcontext_format(*subformats,
						    sub_list[i].field_list,
						    context);
		    if (tmp == NULL) {
			fprintf(stderr, "Format registration failed for subformat \"%s\"\n",
				sub_list[i].format_name);
			return 0;
		    }
		    goto next_format;
		}
		i++;
	    }
	    fprintf(stderr, "Subformat \"%s\" not found in format list\n",
		    *subformats);
	    return 0;
	next_format:
	    free(*subformats);
	    subformats++;
	}
    }
    free(save_subformats);
    return 1;
}

extern void 
CMcomplete_format_registration(format, lock)
CMFormat format;
int lock;
{
    if (lock) CManager_lock(format->cm);
    
    if (CMregister_subformats(format->IOsubcontext, format->field_list,
			      format->subformat_list) != 1) {
	fprintf(stderr, "Format registration failed for format \"%s\"\n",
		format->format_name);
        free_CMFormat (format);
	if (lock) CManager_unlock(format->cm);
	format->format = NULL;
	return;
    }
    format->format = register_opt_format(format->format_name, 
					       format->field_list, format->opt_info, 
						   format->IOsubcontext);
    if (format->format == NULL) {
	fprintf(stderr, "Format registration failed for format \"%s\"\n",
		format->format_name);
        free_CMFormat (format);
	if (lock) CManager_unlock(format->cm);
	return;
    }
    format->registration_pending = 0;
    if (lock) CManager_unlock(format->cm);
}

IOFormat
CMregister_user_format(cm, type_context, format_name, field_list, 
		       subformat_list)
CManager cm;
IOContext type_context;
char *format_name;
IOFieldList field_list;
CMFormatList subformat_list;
{
    CMFormat format;

    if ((field_list == NULL) || (format_name == NULL) 
	|| (cm == NULL) || (type_context == NULL)) 
	return NULL;

    CManager_lock(cm);
    format = CMmalloc(sizeof(struct _CMFormat));
    
    format->cm = cm;
    format->format_name = CMmalloc(strlen(format_name) + 1);
    strcpy(format->format_name, format_name);
    /*  create a new subcontext (to localize name resolution) */
    format->IOsubcontext = type_context;
    format->format = NULL;
    format->field_list_addr = field_list;
    format->handler = (CMHandlerFunc) NULL;
    format->client_data = NULL;
    format->field_list = field_list;
    format->subformat_list = subformat_list;
    format->registration_pending = 1;
    format->opt_info = NULL;
    CMcomplete_format_registration(format, 0);
    if (format->format != NULL) {
	add_user_format_to_cm(cm,format);
	CManager_unlock(cm);
	return format->format;
    } else {
	CMfree(format->format_name);
	CMfree(format);
	return NULL;
    }
}

static void
add_format_to_cm(cm, format)
CManager cm;
CMFormat format;
{
    char *format_name = format->format_name;
    int insert_before = 0, i;

    i = 0;
    for (i=0; i< cm->reg_format_count; i++) {
	int order = strcmp(format_name, cm->reg_formats[i]->format_name);
	if (order < 0) {
	    insert_before = i;
	    break;
	} else if (order == 0) {
	    /* have the same name */
	    IOformat_order suborder;
	    if (format->registration_pending) {
		CMcomplete_format_registration(format, 0);
	    }
	    if (cm->reg_formats[i]->registration_pending) {
		CMcomplete_format_registration(cm->reg_formats[i], 0);
	    }
	    suborder = IOformat_cmp(format->format, 
				    cm->reg_formats[i]->format);
	    if ((suborder == Format_Greater) || 
		(suborder == Format_Incompatible)) {
		insert_before = i;
		break;
	    } else if (suborder == Format_Equal) {
		printf("identical formats!\n");
                insert_before = i;
	    }
	}
    }
    if (i == cm->reg_format_count) {
	insert_before = i;
    }
    cm->reg_formats = CMrealloc(cm->reg_formats, sizeof(CMFormat) * 
				(cm->reg_format_count + 1));
    for (i = cm->reg_format_count; i > insert_before; i--) {
	/* move this up */
	cm->reg_formats[i] = cm->reg_formats[i-1];
    }
    cm->reg_formats[insert_before] = format;
    cm->reg_format_count++;
}

static void
add_user_format_to_cm(cm, format)
CManager cm;
CMFormat format;
{
    char *format_name = name_of_IOformat(format->format);
    int insert_before = 0, i;

    i = 0;    
    for (i=0; i< cm->reg_user_format_count; i++) {
	int order = strcmp(format_name, 
			   name_of_IOformat(cm->reg_user_formats[i]->format));
	if (order < 0) {
	    insert_before = i;
	    break;
	} else if (order == 0) {
	    /* have the same name */
	    IOformat_order suborder;
	    suborder = IOformat_cmp(format->format, 
				    cm->reg_user_formats[i]->format);
	    if ((suborder == Format_Greater) || 
		(suborder == Format_Incompatible)) {
		insert_before = i;
		break;
	    } else if (suborder == Format_Equal) {
                if (cm->reg_user_formats[i]->IOsubcontext == format->IOsubcontext) {
                    printf("identical formats!\n");
                }
                insert_before = i;
                break;
	    }
	}
    }
    if (i == cm->reg_user_format_count) {
	insert_before = i;
    }
    cm->reg_user_formats = CMrealloc(cm->reg_user_formats, sizeof(CMFormat) * 
				(cm->reg_user_format_count + 1));
    for (i = cm->reg_user_format_count; i > insert_before; i--) {
	/* move this up */
	cm->reg_user_formats[i] = cm->reg_user_formats[i-1];
    }
    cm->reg_user_formats[insert_before] = format;
    cm->reg_user_format_count++;

}


extern void
free_CMFormat(format)
CMFormat format;
{
    free_IOsubcontext(format->IOsubcontext);
    CMfree(format);
}

extern CMincoming_format_list
CMidentify_CMformat(cm, format)
CManager cm;
IOFormat format;
{
    int i;
    char *format_name = name_of_IOformat(format);
    IOFieldList native_field_list;
    CMFormatList native_subformat_list;
    IOFormat* subformat_list, *saved_subformat_list;
    int native_struct_size;

    for (i=0; i< cm->reg_format_count; i++) {
	int order = strcmp(format_name, cm->reg_formats[i]->format_name);
	if (order < 0) {
	    return NULL;
	} else if (order == 0) {
	    /* 
	     *  we found a registered format with the same name as the 
	     *  incoming record, is it compatible? 
	     */
	    if (cm->reg_formats[i]->registration_pending) {
		CMcomplete_format_registration(cm->reg_formats[i], 0);
	    }
	    if (cm->reg_formats[i]->format == NULL) {
		continue;
	    }
	    switch(IOformat_cmp(format, cm->reg_formats[i]->format)) {
	    case Format_Equal:
	    case Format_Greater:
		/* 
		 * if the incoming format has the same or more fields
		 * as the registered format, we're cool
		 */
		break;
	    case Format_Less:
	    case Format_Incompatible:
		/* 
		 * if the incoming format has fewer fields than the 
		 * registered handler requires or they're incompatible
		 * keep looking;
		 */
		 continue;
	    }
	    break;
	}
    }

    if (i >= cm->reg_format_count) return NULL;

    native_field_list = cm->reg_formats[i]->field_list;
    native_subformat_list = cm->reg_formats[i]->subformat_list;

    subformat_list = get_subformats_IOformat(format);
    saved_subformat_list = subformat_list;
    while((subformat_list != NULL) && (*subformat_list != NULL)) {
	char *subformat_name = name_of_IOformat(*subformat_list);
	int j = 0;
	while(native_subformat_list && 
	      (native_subformat_list[j].format_name != NULL)) {
	    if (strcmp(native_subformat_list[j].format_name, subformat_name) == 0) {
		IOFieldList sub_field_list;
		int sub_struct_size;
		sub_field_list = native_subformat_list[j].field_list;
		sub_struct_size = struct_size_field_list(sub_field_list, 
							 sizeof(char*));
		set_conversion_IOcontext(cm->IOcontext, *subformat_list,
					 sub_field_list,
					 sub_struct_size);
	    }
	    j++;
	}
	subformat_list++;
    }
    free(saved_subformat_list);
    native_struct_size = struct_size_field_list(native_field_list, 
						sizeof(char*));
    set_conversion_IOcontext(cm->IOcontext, format, native_field_list,
					 native_struct_size);
    cm->in_formats = CMrealloc(cm->in_formats, 
			       sizeof(struct _CMincoming_format) * 
			       (cm->in_format_count + 1));
    cm->in_formats[cm->in_format_count].format = format;
    cm->in_formats[cm->in_format_count].handler = 
	cm->reg_formats[i]->handler;
    cm->in_formats[cm->in_format_count].client_data = 
	cm->reg_formats[i]->client_data;
    return &cm->in_formats[cm->in_format_count++];
}

extern IOFormat 
CMget_IOformat_by_name(cm, context, name)
CManager cm;
IOContext context;
char *name;
{
    IOFormat ret;
    CManager_lock(cm);
    ret = get_IOformat_by_name_IOcontext(context, name);
    CManager_unlock(cm);
    return ret;
}

extern IOFormat 
CMget_format_IOcontext(cm, context, buffer)
CManager cm;
IOContext context;
void *buffer;
{
    IOFormat ret;
    CManager_lock(cm);
    ret = get_format_IOcontext(context, buffer);
    CManager_unlock(cm);
    return ret;
}

extern IOFormat 
CMget_format_app_IOcontext(cm, context, buffer, app_context)
CManager cm;
IOContext context;
void *buffer;
void *app_context;
{
    IOFormat ret;
    CManager_lock(cm);
    ret = get_format_app_IOcontext(context, buffer, app_context);
    CManager_unlock(cm);
    return ret;
}

extern IOFormat *
CMget_subformats_IOcontext(cm, context, buffer)
CManager cm;
IOContext context;
void *buffer;
{
    IOFormat *ret;
    CManager_lock(cm);
    ret = get_subformats_IOcontext(context, buffer);
    CManager_unlock(cm);
    return ret;
}

extern void
CMset_conversion_IOcontext(cm, context, format, field_list,
			   native_struct_size)
CManager cm;
IOContext context;
IOFormat format;
IOFieldList field_list;
int native_struct_size;
{
    CManager_lock(cm);
    set_conversion_IOcontext(context, format, field_list,
			     native_struct_size);
    CManager_unlock(cm);
}

extern int CMself_hosted_formats;

static void
preload_pbio_format(conn, ioformat, context)
CMConnection conn;
IOFormat ioformat;
IOContext context;
{
    IOFormat *subformats, *saved_formats;
    CMtrace_out(conn->cm, CMFormatVerbose, 
		"CMpbio preloading format %s on connection %lx", 
		name_of_IOformat(ioformat), conn);
    saved_formats = subformats = get_subformats_IOformat(ioformat);
    
    while ((subformats != NULL) && (*subformats != NULL)) {
	CMtrace_out(conn->cm, CMFormatVerbose, 
		    "CMpbio sending subformat %s on connection %lx", 
		    name_of_IOformat(*subformats), conn);
	
	if (CMpbio_send_format_preload(*subformats, conn) != 1) {
	    CMtrace_out(conn->cm, CMFormatVerbose, "CMpbio preload failed");
	    return;
	}
#ifndef MODULE
	if (CMtrace_on(conn->cm, CMFormatVerbose)) {
	    int junk;
	    printf("CMpbio Preload is format ");
	    print_server_ID(get_server_ID_IOformat(*subformats, &junk));
	    printf("\n");
	}
#endif
	subformats++;
    }
    free(saved_formats);
}

extern void
CMformat_preload(conn, format)
CMConnection conn;
CMFormat format;
{
    int load_count = 0;
    CMFormat *loaded_list = conn->downloaded_formats;

    if (CMself_hosted_formats == 0) return;
    while (loaded_list && (*loaded_list != NULL)) {
	if (*loaded_list == format) return;
	loaded_list++;
	load_count++;
    }
    
    preload_pbio_format(conn, format->format, format->IOsubcontext);

    if (conn->downloaded_formats == NULL) {
	loaded_list = malloc(2*sizeof(*loaded_list));
    } else {
	loaded_list = realloc(conn->downloaded_formats, 
			      sizeof(*loaded_list) * (load_count + 2));
    }
    loaded_list[load_count] = format;
    loaded_list[load_count+1] = NULL;
    conn->downloaded_formats = loaded_list;
}
