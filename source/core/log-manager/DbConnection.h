#ifndef _DB_CONNECTION_H_
#define _DB_CONNECTION_H_

#include <iostream>
#include <string>
#include <map>
#include <list>

#include <sqlite3.h>

#include <util/ThreadModel.h>

#include "LogManagerSingleton.h"


namespace sf1r {

class DbConnectionBase;

class DbConnection : public LogManagerSingleton<DbConnection>
{
public:
    DbConnection();

    ~DbConnection();

    bool init(const std::string& str);

    /// close all database connections
    void close();

    /// @sql SQL command
    /// @return no result
    /// @throw exception if underlying database reports error
    bool exec(const std::string & sql, bool omitError=false);

    /// @sql SQL command
    /// @results execution results of executing the SQL command
    /// @throw exception if underlying database reports error
    bool exec(const std::string & sql, std::list< std::map<std::string, std::string> > & results, bool omitError=false);

    enum SQL_KEYWORD {
        ATTR_AUTO_INCREMENT, // the attribute to automatically generate unique id for new row
        FUNC_LAST_INSERT_ID, // the function to get the last automatically generated id
        SQL_KEYWORD_NUM
    };

    const std::string& getSqlKeyword(SQL_KEYWORD type) const;

private:
    DbConnectionBase* impl_;
};

}

#endif
