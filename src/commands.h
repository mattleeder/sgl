#ifndef sql_commands
#define sql_commands

#include "pager.h"

int command_db_info(struct Pager *pager);
int command_tables(struct Pager *pager);
int command_sql(struct Pager *pager, const char *command);

#endif