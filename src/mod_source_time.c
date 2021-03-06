/*
 * This file is part of the honeybrid project.
 *
 * 2007-2009 University of Maryland (http://www.umd.edu)
 * Robin Berthier <robinb@umd.edu>, Thomas Coquelin <coquelin@umd.edu>
 * and Julien Vehent <julien@linuxwall.info>
 *
 * 2012-2014 University of Connecticut (http://www.uconn.edu)
 * Tamas K Lengyel <tamas.k.lengyel@gmail.com>
 *
 * Honeybrid is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "modules.h"

/*! mod_source
 \brief check if the source IP has already been seen in a prior connection
 Parameters required:
 function = hash;
 backup   = /etc/honeybrid/source.tb
 \param[in] args, struct that contain the node and the data to process
 \param[in] user_data, not used
 *
 \param[out] set result to 0 if attacker ip is found in search table, 1 if not
 */
mod_result_t mod_source_time(struct mod_args *args) {
    printdbg("%s Module called\n", H(args->pkt->conn->id));

    mod_result_t result = DEFER;
    int expiration = 24 * 3600; /* a day */
    int deny_after = 1200; /* 20 minutes */
    int allow_after = 0; /* accept after this many seconds elapsed since first seeing src */
    gchar *backup_file, *exp, *timef, *timea;
    char *key_src;
    gchar **info;
    GKeyFile *backup;

    GTimeVal t;
    g_get_current_time(&t);
    gint now = (t.tv_sec);

    /*! get the IP address from the packet */
    key_src =  g_malloc0(snprintf(NULL, 0, "%u", args->pkt->packet.ip->saddr)+1);
    sprintf(key_src, "%u", args->pkt->packet.ip->saddr);

    printdbg("%s source IP is %s\n", H(args->pkt->conn->id), key_src);

    /*! get the backup file for this module */
    if (NULL
            == (backup = (GKeyFile *) g_hash_table_lookup(args->node->config,
                    "backup"))) {
        /*! We can't decide */
        printdbg("%s mandatory argument 'backup' undefined!\n",
                H(args->pkt->conn->id));
        return result;
    }
    /*! get the backup file path for this module */
    if (NULL
            == (backup_file = (gchar *) g_hash_table_lookup(args->node->config,
                    "backup_file"))) {
        /*! We can't decide */
        printdbg("%s error, backup file path missing\n",
                H(args->pkt->conn->id));
        return result;
    }

    if ((exp = (char *) g_hash_table_lookup(args->node->config, "expiration"))
            != NULL) {
        expiration = atoi(exp);
    }

    if ((timef = (char *) g_hash_table_lookup(args->node->config, "deny_after"))
            != NULL) {
        deny_after = atoi(timef);
    }

    if ((timea = (char *) g_hash_table_lookup(args->node->config, "allow_after"))
            != NULL) {
        allow_after = atoi(timea);
    }

    if (allow_after >= deny_after) {
        printdbg(
                "%s Misconfiguration: allow_after is greater then deny_after!\n",
                H(args->pkt->conn->id));
        return result;
    }

    printdbg("%s searching for this IP in the database...\n",
            H(args->pkt->conn->id));

    if (NULL == (info = g_key_file_get_string_list(backup, "source", /* generic group name \todo: group by port number? */
    key_src, NULL, NULL))) {
        /*! Unknown IP */
        printdbg("%s IP not found... new entry created\n",
                H(args->pkt->conn->id));

        info = malloc(3 * sizeof(char *));

        /*! 20 characters should be enough to hold even very large numbers */
        info[0] = malloc(20 * sizeof(gchar));
        info[1] = malloc(20 * sizeof(gchar));
        info[2] = malloc(20 * sizeof(gchar));
        g_snprintf(info[0], 20, "1"); /*! counter */
        g_snprintf(info[1], 20, "%d", now); /*! first seen */
        g_snprintf(info[2], 20, "0"); /*! duration */

        if (allow_after == 0)
            result = ACCEPT;
        else
            result = REJECT;

    } else {
        /*! We check if we need to expire this entry */
        int age = atoi(info[2]);
        if (age > expiration) {
            /*! Known IP but entry expired */
            printdbg("%s IP found but expired... entry renewed\n",
                    H(args->pkt->conn->id));

            g_snprintf(info[0], 20, "1"); /*! counter */
            g_snprintf(info[1], 20, "%d", now); /*! first seen */
            g_snprintf(info[2], 20, "0"); /*! duration */

            if (allow_after == 0)
                result = ACCEPT;
            else
                result = REJECT;

        } else {
            /*! Known IP, check time allowed */
            if (atoi(info[1]) + deny_after >= now
                    && atoi(info[1]) + allow_after <= now) {
                printdbg("%s IP found within allowed time-frame\n",
                        H(args->pkt->conn->id));
                result = ACCEPT;
            } else {
                result = REJECT;
                printdbg("%s IP found not withing allowed time-frame\n",
                        H(args->pkt->conn->id));
            }

            g_snprintf(info[0], 20, "%d", atoi(info[0]) + 1); /*! counter */
            g_snprintf(info[2], 20, "%d", now - atoi(info[1])); /*! duration */
        }
    }

    g_key_file_set_string_list(backup, "source", key_src,
            (const gchar * const *) info, 3);

    save_backup(backup, backup_file);

    free(key_src);
    return result;
}

