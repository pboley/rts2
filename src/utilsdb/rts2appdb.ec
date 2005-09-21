#include "rts2appdb.h"
#include "../utils/rts2config.h"

#include <ecpgtype.h>

#include <iostream>
#include <syslog.h>

EXEC SQL INCLUDE sql3types;

Rts2SqlColumn::Rts2SqlColumn (const char *in_sql, const char *in_name)
{
  if (in_sql)
  {
    sql = new char[strlen (in_sql) + 1];
    strcpy (sql, in_sql);
  }
  else
  {
    sql = NULL;
  }

  if (in_name)
  {
    name = new char[strlen (in_name) + 1];
    strcpy (name, in_name);
  }
  else
  {
    name = NULL;
  }
}

Rts2SqlColumn::~Rts2SqlColumn (void)
{
  if (sql)
    delete sql;
  if (name)
    delete name;
}

Rts2SqlQuery::Rts2SqlQuery (const char *in_from)
{
  sql = NULL;
  if (in_from)
  {
    from = new char[strlen (in_from) + 1];
    strcpy (from, in_from);
  }
  else
  {
    from = NULL;
  }
}

Rts2SqlQuery::~Rts2SqlQuery (void)
{
  std::list <Rts2SqlColumn *>::iterator col_iter1, col_iter2;
  if (sql)
    delete sql;
  if (from)
    delete from;
  for (col_iter1 = columns.begin (); col_iter1 != columns.end (); )
  {
    col_iter2 = col_iter1;
    col_iter1++;
    delete *col_iter2;
  }
  columns.clear ();
}

void
Rts2SqlQuery::addColumn (Rts2SqlColumn *add_column)
{
  columns.push_back (add_column);
  if (sql)
    delete sql;
  sql = NULL;
}

void
Rts2SqlQuery::addColumn (const char *in_sql, const char *in_name)
{
  Rts2SqlQuery::addColumn (new Rts2SqlColumn (in_sql, in_name));
}

char *
Rts2SqlQuery::genSql ()
{
  size_t len = 2000;
  size_t used_len = 0;
  std::list <Rts2SqlColumn *>::iterator col_iter1;
  if (sql)
    return sql;
  sql = new char[len];
  strcpy (sql, "select ");
  used_len = strlen (sql);
  for (col_iter1 = columns.begin (); col_iter1 != columns.end (); col_iter1++)
  {
    Rts2SqlColumn *col = (*col_iter1);
    char *col_sql = col->genSql ();
    if (used_len + strlen (col_sql) + 2 >= len)
    {
      char *new_sql;
      len += 2000;
      new_sql = new char[len];
      strcpy (new_sql, sql);
      delete sql;
      sql = new_sql;
    }
    if (col_iter1 != columns.begin ())
      strcat (sql, ", ");
    strcat (sql, col_sql);
    used_len += strlen (col_sql);
  }
  // add from
  if (from)
  {
    if (used_len + 6 + strlen (from) >= len)
    {
      char *new_sql;
      len += 2000;
      new_sql = new char[len];
      strcpy (new_sql, sql);
      delete sql;
      sql = new_sql;
    }
    strcat (sql, " from ");
    strcat (sql, from);
    used_len += 6 + strlen (from);
  }
  return sql;
}

void
Rts2SqlQuery::display ()
{
  EXEC SQL BEGIN DECLARE SECTION;
  char *stmp;
  int row;
  int cols;
  int cur_col;
  int col_len, col_car, scale, precision;
  int type;

  bool d_bool;
  int d_int;
  float d_float;
  double d_double;
  char d_string[1024];
  EXEC SQL END DECLARE SECTION;

  std::list <Rts2SqlColumn *>::iterator col_iter;

  EXEC SQL ALLOCATE DESCRIPTOR disp_desc;

  std::cout << genSql () << std::endl;
  // try to execute query
  stmp = genSql ();

  EXEC SQL PREPARE disp_stmp FROM :stmp;

  EXEC SQL DECLARE disp_cur CURSOR FOR disp_stmp;
  EXEC SQL OPEN disp_cur;

  row = 0;
  
  while (1)
  {
    EXEC SQL FETCH next FROM disp_cur INTO DESCRIPTOR disp_desc;

    if (sqlca.sqlcode)
    {
      if (sqlca.sqlcode != ECPG_NOT_FOUND)
        std::cerr << "Sql error " << sqlca.sqlerrm.sqlerrmc << std::endl;
      break;
    }

    EXEC SQL GET DESCRIPTOR disp_desc :cols = COUNT;

    if (row == 0)
    {
      // print header..
      std::cout << "-----------------------------------" << std::endl;
      cur_col = 1;
      for (col_iter = columns.begin (); col_iter != columns.end (); col_iter++)
      {
	if (cur_col > 1)
	  std::cout << "|";
	std::cout << (*col_iter)->getHeader ();
	cur_col++;
      }
      std::cout << std::endl;
    }

    row++;

    for (cur_col = 1; cur_col <= cols; cur_col++)
    {
      if (cur_col > 1)
	std::cout << " | ";
      EXEC SQL GET DESCRIPTOR disp_desc VALUE :cur_col
      	:type = TYPE,
        :col_len = OCTET_LENGTH,
	:scale = SCALE,
	:precision = PRECISION,
        :col_car = CARDINALITY;
      // unsigned short..
      switch (type)
      {
	case SQL3_BOOLEAN:
	  EXEC SQL GET DESCRIPTOR disp_desc VALUE :cur_col :d_bool = DATA;
	  std::cout << (d_bool ? "true" : "false");
	  break;
	case SQL3_NUMERIC:
	case SQL3_DECIMAL:
	  if (scale == 0)
	  {
	    EXEC SQL GET DESCRIPTOR disp_desc VALUE :cur_col :d_int = DATA;
	    std::cout << d_int;
	  }
	  else
	  {
	    EXEC SQL GET DESCRIPTOR disp_desc VALUE :cur_col :d_double = DATA;
	    std::cout << d_double;
	  }
	  break;
	case SQL3_INTEGER:
	case SQL3_SMALLINT:
	  EXEC SQL GET DESCRIPTOR disp_desc VALUE :cur_col :d_int = DATA;
	  std::cout << d_int;
	  break;
	case SQL3_FLOAT:
	case SQL3_REAL:
	  EXEC SQL GET DESCRIPTOR disp_desc VALUE :cur_col :d_float = DATA;
	  std::cout << d_float;
	  break;
	case SQL3_DOUBLE_PRECISION:
	  EXEC SQL GET DESCRIPTOR disp_desc VALUE :cur_col :d_double = DATA;
	  std::cout << d_double;
	  break;
	case SQL3_DATE_TIME_TIMESTAMP:
	case SQL3_INTERVAL:
	case SQL3_CHARACTER:
	case SQL3_CHARACTER_VARYING:
	default:
	  EXEC SQL GET DESCRIPTOR disp_desc VALUE :cur_col :d_string = DATA;
	  std::cout << d_string;
	  break;
      }
    }
    std::cout << std::endl;
  }

  EXEC SQL CLOSE disp_cur;
  EXEC SQL DEALLOCATE DESCRIPTOR disp_desc;
}

Rts2AppDb::Rts2AppDb (int in_argc, char **in_argv) : Rts2App (in_argc, in_argv)
{
  connectString = NULL;
  configFile = NULL;

  addOption ('b', "database", 1, "connect string to PSQL database (default to stars)");
  addOption ('c', "config", 1, "configuration file");
}

Rts2AppDb::~Rts2AppDb ()
{
  EXEC SQL DISCONNECT;
  if (connectString)
    delete connectString;
}

int
Rts2AppDb::processOption (int in_opt)
{
  switch (in_opt)
  {
    case 'b':
      connectString = new char[strlen (optarg) + 1];
      strcpy (connectString, optarg);
      break;
    case 'c':
      configFile = optarg;
      break;
    default:
      return Rts2App::processOption (in_opt);
  }
  return 0;
}

int
Rts2AppDb::initDB ()
{
  int ret;
  EXEC SQL BEGIN DECLARE SECTION;
  char conn_str[200];
  EXEC SQL END DECLARE SECTION;
  // try to connect to DB

  Rts2Config *config;

  // load config..

  config = Rts2Config::instance ();
  ret = config->loadFile (configFile);
  if (ret)
    return ret;
  
  if (connectString)
  {
    strncpy (conn_str, connectString, 200);
  }
  else
  {
    config->getString ("database", "name", conn_str, 200);
  }
  conn_str[199] = '\0';

  EXEC SQL CONNECT TO :conn_str;
  if (sqlca.sqlcode != 0)
  {
    syslog (LOG_ERR, "Rts2DeviceDb::init Cannot connect to DB %s: %s", conn_str, sqlca.sqlerrm.sqlerrmc); 
    return -1;
  }

  return 0;
}

int
Rts2AppDb::init ()
{
  int ret;

  ret = Rts2App::init ();
  if (ret)
    return ret;

  // load config.
  return initDB ();
}
