/* -*- Mode: C; tab-width: 4; indent-tabs-mode: t; c-basic-offset: 4 -*- */
/*
 *    monitor.c
 *    This file is part of "Sauvegarde" project.
 *
 *    (C) Copyright 2014 - 2015 Olivier Delhomme
 *     e-mail : olivier.delhomme@free.fr
 *
 *    "Sauvegarde" is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    "Sauvegarde" is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with "Sauvegarde".  If not, see <http://www.gnu.org/licenses/>
 */

/**
 * @file monitor.c
 *
 * This file is the main file for the monitor program. This monitor
 * program has to monitor file changes onto filesystems. It should notice
 * when a file is created, deleted or changed
 */

#include "monitor.h"



static main_struct_t *init_main_structure(options_t *opt);

static GSList *calculate_hash_list_for_file(GFile *a_file, gint64 blocksize);
static meta_data_t *get_meta_data_from_fileinfo(gchar *directory, GFileInfo *fileinfo, GFile *a_file, gint64 blocksize);
static void iterate_over_enum(main_struct_t *main_struct, gchar *directory, GFileEnumerator *file_enum);
static void carve_one_directory(gpointer data, gpointer user_data);
static void carve_all_directories(main_struct_t *main_struct);




/**
 * Inits the main structure.
 * @note With sqlite version > 3.7.7 we should use URI filename.
 * @param opt : a filled options_t * structure that contains all options
 *        by default, read into the file or selected in the command line.
 * @returns a main_struct_t * pointer to the main structure
 */
static main_struct_t *init_main_structure(options_t *opt)
{
    main_struct_t *main_struct = NULL;
    gchar *db_uri = NULL;
    gchar *conn = NULL;

    if (opt != NULL)
        {

            print_debug(_("Please wait while initializing main structure...\n"));

            main_struct = (main_struct_t *) g_malloc0(sizeof(main_struct_t));

            create_directory(opt->dircache);
            db_uri = g_build_filename(opt->dircache, opt->dbname , NULL);
            main_struct->database = open_database(db_uri);

            main_struct->opt = opt;
            main_struct->hostname = g_get_host_name();
            main_struct->queue = g_async_queue_new();
            main_struct->store_queue = g_async_queue_new();

            main_struct->hashs = get_all_inserted_hashs(main_struct->database);


            if (opt != NULL && opt->ip != NULL)
                {
                    conn = make_connexion_string(opt->ip, opt->port);
                    main_struct->comm = init_comm_struct(conn);
                }
            else
                {
                    /* This should never happen because we have default values */
                    main_struct->comm = NULL;
                }

            main_struct->signal_fd = start_signals();
            main_struct->fanotify_fd = start_fanotify(opt);

            print_debug(_("Main structure initialized !\n"));

        }

    return main_struct;
}

/************************************************/


/**
 * Calculates hashs for each block of blocksize bytes long on the file
 * and returns a list of all hashs in correct order stored in a binary
 * form to save space.
 * @param a_file is the file from which we want the hashs.
 * @param blocksize is the blocksize to be used to calculate hashs upon.
 * @returns a GSList * list of hashs stored in a binary form.
 */
static GSList *calculate_hash_list_for_file(GFile *a_file, gint64 blocksize)
{
    GFileInputStream *stream = NULL;
    GError *error = NULL;
    GSList *hash_list = NULL;

    gssize read = 0;
    guchar *buffer = NULL;
    GChecksum *checksum = NULL;
    guint8 *a_hash = NULL;
    gsize digest_len = HASH_LEN;


    if (a_file != NULL)
        {
            stream = g_file_read(a_file, NULL, &error);

            if (stream != NULL && error == NULL)
                {

                    checksum = g_checksum_new(G_CHECKSUM_SHA256);
                    buffer = (guchar *) g_malloc0 (blocksize);
                    a_hash = (guint8 *) g_malloc0 (digest_len);

                    read = g_input_stream_read((GInputStream *) stream, buffer, blocksize, NULL, &error);

                    while (read != 0 && error == NULL)
                        {
                            g_checksum_update(checksum, buffer, read);
                            g_checksum_get_digest(checksum, a_hash, &digest_len);

                            /* Need to save data and read in hashs_data_t structure */
                            hash_list = g_slist_prepend(hash_list, a_hash);
                            g_checksum_reset(checksum);
                            digest_len = HASH_LEN;
                            read = g_input_stream_read((GInputStream *) stream, buffer, blocksize, NULL, &error);
                        }

                    if (error != NULL)
                        {
                            print_error(__FILE__, __LINE__, _("Error while reading file: %s\n"), error->message);
                            error = free_error(error);
                            hash_list = free_list(hash_list);
                        }

                    /* get the list in correct order (because we prepended the hashs to get speed when inserting hashs in the list) */
                    hash_list = g_slist_reverse(hash_list);

                    free_variable(a_hash);
                    free_variable(buffer);
                    g_checksum_free(checksum);
                    g_input_stream_close((GInputStream *) stream, NULL, NULL);
                    free_object(stream);
                }
            else
                {
                    print_error(__FILE__, __LINE__, _("Unable to open file for reading: %s\n"), error->message);
                    error = free_error(error);
                }
        }

    return hash_list;
}


/**
 * Gets all meta data for a file and returns a filled meta_data_t *
 * structure.
 * @param directory is the directory we are iterating over it is used
 *        here to build the filename name.
 * @param fileinfo is a glib structure that contains all meta datas and
 *        more for a file.
 * @param a_file is the corresponding GFile pointer of fileinfo's
 *        structure.
 * @param blocksize is the blocksize to be used to calculate hashs upon.
 * @returns a newly allocated and filled meta_data_t * structure.
 */
static meta_data_t *get_meta_data_from_fileinfo(gchar *directory, GFileInfo *fileinfo, GFile *a_file, gint64 blocksize)
{
    meta_data_t *meta = NULL;

    if (directory != NULL && fileinfo != NULL)
        {
            /* filling meta data for the file represented by fileinfo */
            meta = new_meta_data_t();

            meta->file_type = g_file_info_get_file_type(fileinfo);
            meta->name = g_build_path(G_DIR_SEPARATOR_S, directory, g_file_info_get_name(fileinfo), NULL);
            meta->inode = g_file_info_get_attribute_uint64(fileinfo, G_FILE_ATTRIBUTE_UNIX_INODE);
            meta->owner = g_file_info_get_attribute_as_string(fileinfo, G_FILE_ATTRIBUTE_OWNER_USER);
            meta->group = g_file_info_get_attribute_as_string(fileinfo, G_FILE_ATTRIBUTE_OWNER_GROUP);
            meta->uid = g_file_info_get_attribute_uint32(fileinfo, G_FILE_ATTRIBUTE_UNIX_UID);
            meta->gid = g_file_info_get_attribute_uint32(fileinfo, G_FILE_ATTRIBUTE_UNIX_GID);
            meta->atime = g_file_info_get_attribute_uint64(fileinfo, G_FILE_ATTRIBUTE_TIME_ACCESS);
            meta->ctime = g_file_info_get_attribute_uint64(fileinfo, G_FILE_ATTRIBUTE_TIME_CHANGED);
            meta->mtime = g_file_info_get_attribute_uint64(fileinfo, G_FILE_ATTRIBUTE_TIME_MODIFIED);
            meta->mode = g_file_info_get_attribute_uint32(fileinfo, G_FILE_ATTRIBUTE_UNIX_MODE);
            meta->size = g_file_info_get_attribute_uint64(fileinfo, G_FILE_ATTRIBUTE_STANDARD_SIZE);

             /* Do the right things with specific cases */
            if (meta->file_type == G_FILE_TYPE_SYMBOLIC_LINK)
                {
                    meta->link = g_file_info_get_attribute_byte_string(fileinfo, G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET);
                }
            else if (meta->file_type == G_FILE_TYPE_REGULAR)
                {
                    /* Need to save buffer also in hash_data_t list */
                    meta->hash_list = calculate_hash_list_for_file(a_file, blocksize);
                }
        }

    return meta;
}


/**
 * Iterates over an enumerator obtained from a directory.
 * @param main_struct : main structure of the program
 * @param directory is the directory we are iterating over
 * @param file_enum is the enumerator obtained when opening a directory
 *        to carve it.
 */
static void iterate_over_enum(main_struct_t *main_struct, gchar *directory, GFileEnumerator *file_enum)
{
    GFile *a_file = NULL;
    GError *error = NULL;
    GFileInfo *fileinfo = NULL;
    gchar *filename = NULL;
    meta_data_t *meta = NULL;
    gint64 blocksize = CISEAUX_BLOCK_SIZE;

    if (main_struct != NULL && file_enum != NULL)
        {
            if (main_struct->opt != NULL)
                {
                    blocksize = main_struct->opt->blocksize;
                }

            fileinfo = g_file_enumerator_next_file(file_enum, NULL, &error);

            while (error == NULL && fileinfo != NULL)
                {
                    a_file = g_file_enumerator_get_child(file_enum, fileinfo);
                    meta = get_meta_data_from_fileinfo(directory, fileinfo, a_file, blocksize);

                    /* Send the meta datas       */
                    /* Save them to the db cache */


                    if (meta->file_type == G_FILE_TYPE_DIRECTORY)
                        {
                            /* recursive call */
                            carve_one_directory(filename, main_struct);
                        }

                    /* free meta_data along with fileinfo */
                    meta = free_meta_data_t(meta);
                    fileinfo = free_object(fileinfo);
                    fileinfo = g_file_enumerator_next_file(file_enum, NULL, &error);
                }
        }
}


/**
 * Call back for the g_slist_foreach function that carves one directory
 * and sub directories in a recursive way.
 * @param data is an element of opt->list ie: a gchar * that represents
 *        a directory name
 * @param user_data is the main_struct_t * pointer to the main structure.
 */
static void carve_one_directory(gpointer data, gpointer user_data)
{
    gchar *directory = (gchar *) data;
    main_struct_t *main_struct = (main_struct_t *) user_data;

    GFile *a_dir = NULL;
    GFileEnumerator *file_enum = NULL;
    GError *error = NULL;


    a_dir = g_file_new_for_path(directory);
    file_enum = g_file_enumerate_children(a_dir, "*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, &error);

    if (error == NULL && file_enum != NULL)
        {
            iterate_over_enum(main_struct, directory, file_enum);
            g_file_enumerator_close(file_enum, NULL, NULL);
            file_enum = free_object(file_enum);
        }
    else
        {
            print_error(__FILE__, __LINE__, _("Unable to enumerate directory %s: %s\n"), directory, error->message);
            error = free_error(error);
        }

    a_dir = free_object(a_dir);
}


/**
 * Does carve all directories from the list in the option list
 * @param main_struct : main structure of the program that contains also
 *        the options structure that should have a list of directories
 *        to save.
 */
static void carve_all_directories(main_struct_t *main_struct)
{
    if (main_struct != NULL && main_struct->opt != NULL)
        {
            g_slist_foreach(main_struct->opt->dirname_list, carve_one_directory, main_struct);
        }
}


/**
 * Main function
 * @param argc : number of arguments given on the command line.
 * @param argv : an array of strings that contains command line arguments.
 * @returns always 0
 */
int main(int argc, char **argv)
{
    options_t *opt = NULL;  /** Structure to manage options from the command line can be freed when no longer needed */
    main_struct_t *main_struct = NULL;

    #if !GLIB_CHECK_VERSION(2, 36, 0)
        g_type_init();  /** g_type_init() is deprecated since glib 2.36 */
    #endif

    init_international_languages();
    curl_global_init(CURL_GLOBAL_ALL);

    opt = do_what_is_needed_from_command_line_options(argc, argv);

    if (opt != NULL)
        {
            main_struct = init_main_structure(opt);

            carve_all_directories(main_struct);

            /** Launching an infinite loop to get modifications done on
             * the filesystem (on directories we watch).
             * @note fanotify's kernel interface does not provide the events
             * needed to know if a file has been deleted or it's attributes
             * changed. Enabling this feature even if we know that files
             * will never get deleted in our database.
             */
            fanotify_loop(main_struct);

            free_options_t_structure(main_struct->opt);
        }

    return 0;
}
