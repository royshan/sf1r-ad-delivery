#include "ProductQueryIntent.h"
#include "QueryIntentHelper.h"

#include <common/Keys.h>

#include <boost/unordered_map.hpp>
#include <map>
#include <list>

#include <glog/logging.h>

namespace sf1r 
{
using driver::Keys;
using namespace izenelib::driver;

ProductQueryIntent::ProductQueryIntent(IntentContext* context)
    : QueryIntent(context)
{
    for (std::size_t i = 0; i < classifiers_.size(); i++)
    {
        classifiers_[i].clear();
    }
}

ProductQueryIntent::~ProductQueryIntent()
{
    for (std::size_t i = 0; i < classifiers_.size(); i++)
    {
        ContainerIterator it = classifiers_[i].begin();
        for(; it != classifiers_[i].end(); it++)
        {
            if(*it)
            {
                delete *it;
                *it = NULL;
            }
        }
    }
    classifiers_.clear();
}

void ProductQueryIntent::process(izenelib::driver::Request& request, izenelib::driver::Response& response)
{
    if (classifiers_.empty())
        return;
    std::string keywords = asString(request[Keys::search][Keys::keywords]);
    if (keywords.empty() || "*" == keywords)
        return;
   
    izenelib::driver::Value& conditions = request[Keys::conditions];
    const izenelib::driver::Value::ArrayType* array = conditions.getPtr<Value::ArrayType>();
    boost::unordered_map<std::string, bool> bitmap;
    if (array && (0 != array->size()))
    {
        for (std::size_t i = 0; i < array->size(); i++)
        {
            std::string condition = asString((*array)[i][Keys::property]);
            for (std::size_t priority = 0; priority < classifiers_.size(); priority++)
            {
                ContainerIterator it = classifiers_[priority].begin();
                for (; it != classifiers_[priority].end(); it++)
                {
                    if (condition == (*it)->name())
                    {
                        bitmap.insert(make_pair(condition, false));
                        break;
                    }
                }
                if (it != classifiers_[priority].end())
                    break;
            }
        }
    }
    
    izenelib::driver::Value& disables = request["query_intent"];
    array = disables.getPtr<Value::ArrayType>();
    if (array && (0 != array->size()))
    {
        for (std::size_t i = 0; i < array->size(); i++)
        {
            std::string condition = asString((*array)[i][Keys::property]);
            for (std::size_t priority = 0; priority < classifiers_.size(); priority++)
            {
                ContainerIterator it = classifiers_[priority].begin();
                for (; it != classifiers_[priority].end(); it++)
                {
                    if (condition == (*it)->name())
                    {
                        bitmap.insert(make_pair(condition, false));
                        break;
                    }
                }
                if (it != classifiers_[priority].end())
                    break;
            }
        }
    }
    
    std::map<QueryIntentCategory, std::list<std::string> > intents;
    bool ret = false;
    for (std::size_t priority = 0; priority < classifiers_.size(); priority++)
    {
        ContainerIterator it = classifiers_[priority].begin();
        for(; it != classifiers_[priority].end(); it++)
        {
            if (bitmap.end() == bitmap.find((*it)->name()))
                ret |= (*it)->classify(intents, keywords);
        }
    }
    bitmap.clear();
  
    if (!ret)
        return;
    request[Keys::search][Keys::keywords] = keywords;
    
    boost::trim(keywords);
    if (keywords.empty() )
        request[Keys::search][Keys::keywords] = "*";
    refineRequest(request, response, intents);
}

void ProductQueryIntent::reloadLexicon()
{
    for (std::size_t i = 0; i < classifiers_.size(); i++)
    {
        ContainerIterator it = classifiers_[i].begin();
        for(; it != classifiers_[i].end(); it++)
        {
            if(*it)
            {
                (*it)->reloadLexicon();
            }
        }
    }
}

}
