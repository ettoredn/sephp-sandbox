/*
   +----------------------------------------------------------------------+
   | PHP Version 5                                                        |
   +----------------------------------------------------------------------+
   | Copyright (c) 1997-2011 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Authors: Ettore Del Negro <write@ettoredelnegro.me>                  |
   +----------------------------------------------------------------------+
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_variables.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_selinux.h"

ZEND_DECLARE_MODULE_GLOBALS(selinux)

void (*old_php_import_environment_variables)(zval *array_ptr TSRMLS_DC);
void selinux_php_import_environment_variables(zval *array_ptr TSRMLS_DC);

void (*old_zend_execute)(zend_op_array *op_array TSRMLS_DC);
void selinux_zend_execute(zend_op_array *op_array TSRMLS_DC);

zend_op_array *(*old_zend_compile_file)(zend_file_handle *file_handle, int type TSRMLS_DC);
zend_op_array *selinux_zend_compile_file(zend_file_handle *file_handle, int type TSRMLS_DC);


/*
 * Every user visible function must have an entry in selinux_functions[].
 */
const zend_function_entry selinux_functions[] = {
	{NULL, NULL, NULL}
};

zend_module_entry selinux_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"selinux",
	selinux_functions,
	PHP_MINIT(selinux),
	PHP_MSHUTDOWN(selinux),
	PHP_RINIT(selinux),
	PHP_RSHUTDOWN(selinux),
	PHP_MINFO(selinux),
#if ZEND_MODULE_API_NO >= 20010901
	"0.1",
#endif
	STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_SELINUX
ZEND_GET_MODULE(selinux)
#endif

PHP_MINIT_FUNCTION(selinux)
{
	// Adds FastCGI parameters to catch
	SELINUX_G(fcgi_params[SELINUX_PARAM_SELINUX_DOMAIN_IDX]) = SELINUX_PARAM_SELINUX_DOMAIN_NAME;
	
	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(selinux)
{
	return SUCCESS;
}

PHP_RINIT_FUNCTION(selinux)
{
	/* Override php_import_environment_variables ( main/php_variables.c:824 ) */
	old_php_import_environment_variables = php_import_environment_variables;
	php_import_environment_variables = selinux_php_import_environment_variables;

	/* Override zend_execute to execute it in a SELinux context */
	old_zend_execute = zend_execute;
	zend_execute = selinux_zend_execute;
	
	/* Override zend_compile_file to check read permission on it for currenct SELinux domain */
	old_zend_compile_file = zend_compile_file;
	zend_compile_file = selinux_zend_compile_file;
		
	return SUCCESS;
}

PHP_RSHUTDOWN_FUNCTION(selinux)
{
	php_import_environment_variables = old_php_import_environment_variables;
	zend_execute = old_zend_execute;
	zend_compile_file = old_zend_compile_file;
	
	return SUCCESS;
}

PHP_MINFO_FUNCTION(selinux)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "SELinux support", "enabled");
	php_info_print_table_end();
}

zend_op_array *selinux_zend_compile_file(zend_file_handle *file_handle, int type TSRMLS_DC)
{
	// @DEBUG
	char buf[500];
	memset( buf, 0, sizeof(buf) );
	sprintf( buf, "[*] Compiling %s <br>", file_handle->filename );
	PHPWRITE( buf, strlen(buf) );
	
	return old_zend_compile_file( file_handle, type TSRMLS_CC );
}


/*
 * struct zend_op_array 	@ Zend/zend_compile.h:191
 * struct zend_execute_data @ Zend/zend_compile.h:308
 */
void selinux_zend_execute(zend_op_array *op_array TSRMLS_DC)
{
	// zend_execute_data *edata = EG(current_execute_data);

	// @DEBUG
	char buf[500];
	memset( buf, 0, sizeof(buf) );
	sprintf( buf, "[*] Executing %s <br>", op_array->filename );
	PHPWRITE( buf, strlen(buf) );
	
	old_zend_execute(op_array TSRMLS_CC);
}

void selinux_php_import_environment_variables(zval *array_ptr TSRMLS_DC)
{
	zval **data;
	HashTable *arr_hash;
	HashPosition pointer;
	int i;
	
	// @DEBUG
	char buf[500];
	memset( buf, 0, sizeof(buf) );
	sprintf( buf, "[*] Hijacking environment variables import..<br>" );
	PHPWRITE( buf, strlen(buf) );
	
	/* call php's original import as a catch-all */
	old_php_import_environment_variables(array_ptr TSRMLS_CC);
	
	arr_hash = Z_ARRVAL_P(array_ptr);
	for (zend_hash_internal_pointer_reset_ex(arr_hash, &pointer); 
		zend_hash_get_current_data_ex(arr_hash, (void**) &data, &pointer) == SUCCESS; 
		zend_hash_move_forward_ex(arr_hash, &pointer))
	{
		char *key;
		int key_len;
		long index;
		
		if (zend_hash_get_current_key_ex(arr_hash, &key, &key_len, &index, 0, &pointer) == HASH_KEY_IS_STRING)
		{
			for (i=0; i < SELINUX_PARAMS_COUNT; i++)
			{
				/*
				 * Apache mod_fastcgi adds a parameter for every SetEnv <name> <value>
				 * in the form of "REDIRECT_<name>". These need to be hidden too.
				 */
				int redirect_len = strlen("REDIRECT_") + strlen( SELINUX_G(fcgi_params[i]) ) + 1;
				char *redirect_param = (char *) emalloc( redirect_len );
				
				memset( redirect_param, 0, redirect_len );
				strcat( redirect_param, "REDIRECT_" );
				strcat( redirect_param, SELINUX_G(fcgi_params[i]) );
					
				if (!strncmp( key, SELINUX_G(fcgi_params[i]), strlen( SELINUX_G(fcgi_params[i]) )))
				{
					// TODO handle of other types (int, null, etc) if needed
					if (Z_TYPE_PP(data) == IS_STRING)
					SELINUX_G(fcgi_values[i]) = Z_STRVAL_PP(data);
					
					// @DEBUG
					memset( buf, 0, sizeof(buf) );
					sprintf( buf, "[*] Got %s => %s <br>", SELINUX_G(fcgi_params[i]), SELINUX_G(fcgi_values[i]) );
					PHPWRITE( buf, strlen(buf) );
					
					// Hide <selinux_param>
					zend_hash_del(arr_hash, key, strlen(key) + 1);
				}
				
				// Hide REDIRECT_<selinux_param> entries
				if (!strncmp( key, redirect_param, redirect_len ))
					zend_hash_del(arr_hash, key, strlen(key) + 1);
				
				efree( redirect_param );
			}
		}
	}
}
