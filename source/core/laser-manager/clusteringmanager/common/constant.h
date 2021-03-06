/*
 * constant.h
 *
 *  Created on: Mar 31, 2014
 *      Author: alex
 */

#ifndef SF1R_LASER_CONSTANT_H_
#define SF1R_LASER_CONSTANT_H_

#include <am/sequence_file/ssfr.h>
namespace sf1r
{
namespace laser
{
namespace clustering
{
typedef int hash_t;
typedef izenelib::am::ssf::Writer<> izene_writer;
typedef izenelib::am::ssf::Reader<> izene_reader;
typedef izene_reader* izene_reader_pointer;
typedef izene_writer* izene_writer_pointer;
#define RETURN_ON_FAILURE(condition)                                        \
if (! (condition))                                                          \
{                                                                           \
    return false;                                                           \
}
enum STATUS
{
    ONLY_READ,
    TRUNCATED,
    APPEND
};
}
}
}
#endif /* CONSTANT_H_ */
