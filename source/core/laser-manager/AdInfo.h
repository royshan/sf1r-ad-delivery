#ifndef SF1R_LASER_ADINFO_H
#define SF1R_LASER_ADINFO_H
#include <3rdparty/msgpack/msgpack.hpp>
#include "SparseVector.h"

namespace sf1r { namespace laser {
class AdInfo
{
public:
    std::string& DOCID() 
    {
        return DOCID_;
    }

    std::string& clusteringId()
    {
        return clusteringId_;
    }

    std::vector<int>& index()
    {
        return vector_.index();
    }

    std::vector<float>& value()
    {
        return vector_.value();
    }
    
    MSGPACK_DEFINE(DOCID_, clusteringId_, vector_);
private:
    std::string DOCID_;
    std::string clusteringId_;
    SparseVector vector_;
};

} }
#endif
