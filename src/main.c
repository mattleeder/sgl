#include <stdio.h>
#include <string.h>

#include "commands.h"
#include "sql_utils.h"
#include "pager.h"

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: ./your_program.sh <database path> <command>\n");
        return 1;
    }

    const char *database_file_path = argv[1];
    const char *command = argv[2];

    struct Pager *pager = pager_open(database_file_path);

    int result = 0;

    if (strcmp(command, ".dbinfo") == 0) {
        fprintf(stderr, "Received dbinfo command\n");
        result = command_db_info(pager);

    } else if (strcmp(command, ".tables") == 0) {
        fprintf(stderr, "Received tables command\n");
        result = command_tables(pager);

    } else if (strncmp(command, ".", 1) != 0) {
        fprintf(stderr, "Received SQL statement\n");
        result = command_sql(pager, command);

    } else {
        fprintf(stderr, "Unknown command %s\n", command);
        result = 1;
    }

    pager_close(pager);
    return result;
}