#include "SummarizationSubManager.h"
#include "ParentKeyStorage.h"
#include "SummarizationStorage.h"
#include "CommentCacheStorage.h"
#include "splm.h"

#include <index-manager/IndexManager.h>
#include <document-manager/DocumentManager.h>

#include <common/ScdParser.h>
#include <idmlib/util/idm_analyzer.h>

#include <boost/filesystem.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/algorithm/string.hpp>

#include <glog/logging.h>

#include <iostream>
#include <vector>
#include <algorithm>

using namespace izenelib::ir::indexmanager;
namespace bfs = boost::filesystem;

namespace sf1r
{

static const UString DOCID("DOCID", UString::UTF_8);

bool CheckParentKeyLogFormat(
        const SCDDocPtr& doc,
        const UString& parent_key_name)
{
    if (doc->size() != 2) return false;
    const UString& first = (*doc)[0].first;
    const UString& second = (*doc)[1].first;
    //FIXME case insensitive compare, but it requires extra string conversion,
    //which introduces unnecessary memory fragments
    return (first == DOCID && second == parent_key_name);
}

struct IsParentKeyFilterProperty
{
    const std::string& parent_key_property;

    IsParentKeyFilterProperty(const std::string& property)
        : parent_key_property(property)
    {}

    bool operator()(const QueryFiltering::FilteringType& filterType)
    {
        return boost::iequals(parent_key_property, filterType.first.second);
    }
};


MultiDocSummarizationSubManager::MultiDocSummarizationSubManager(
        const std::string& homePath,
        SummarizeConfig schema,
        boost::shared_ptr<DocumentManager> document_manager,
        boost::shared_ptr<IndexManager> index_manager,
        idmlib::util::IDMAnalyzer* analyzer)
    : schema_(schema)
    , parent_key_ustr_name_(schema_.parentKey, UString::UTF_8)
    , document_manager_(document_manager)
    , index_manager_(index_manager)
    , analyzer_(analyzer)
    , parent_key_storage_(new ParentKeyStorage(homePath + "/parentkey"))
    , summarization_storage_(new SummarizationStorage(homePath + "/summarization"))
    , comment_cache_storage_(new CommentCacheStorage(homePath + "/comment_cache"))
    , comment_buffer_size_(0)
    , corpus_(new Corpus())
{
    if (!schema_.parentKeyLogPath.empty())
        bfs::create_directories(schema_.parentKeyLogPath);
}

MultiDocSummarizationSubManager::~MultiDocSummarizationSubManager()
{
    delete parent_key_storage_;
    delete summarization_storage_;
    delete comment_cache_storage_;
    delete corpus_;
}

void MultiDocSummarizationSubManager::EvaluateSummarization()
{
    BuildIndexOfParentKey_();
    if (schema_.parentKeyLogPath.empty())
    {
        ///No parentKey, directly build summarization
        for (uint32_t i = 1; i <= document_manager_->getMaxDocId(); i++)
        {
            Document doc;
            document_manager_->getDocument(i, doc);
            Document::property_const_iterator kit = doc.findProperty(schema_.foreignKeyPropName);
            if (kit == doc.propertyEnd())
                continue;

            Document::property_const_iterator cit = doc.findProperty(schema_.contentPropName);
            if (cit == doc.propertyEnd())
                continue;

            const UString& key = kit->second.get<UString>();
            const UString& content = cit->second.get<UString>();
            CommentBufferItemType& cache_item = comment_buffer_[key];
            cache_item.push_back(make_pair(i, UString()));
            cache_item.back().second.assign(content);

            ++comment_buffer_size_;
            if (IsCommentBufferFull_())
                FlushCommentBuffer_();
        }
    }
    else
    {
        for (uint32_t i = 1; i <= document_manager_->getMaxDocId(); i++)
        {
            Document doc;
            document_manager_->getDocument(i, doc);
            //FIXME
            Document::property_const_iterator pit = doc.findProperty(schema_.parentKey);
            if (pit == doc.propertyEnd())
                continue;

            Document::property_const_iterator cit = doc.findProperty(schema_.contentPropName);
            if (cit == doc.propertyEnd())
                continue;

            const UString& parent_key = pit->second.get<UString>();
            const UString& content = cit->second.get<UString>();
            CommentBufferItemType& cache_item = comment_buffer_[parent_key];
            cache_item.push_back(make_pair(i, UString()));
            cache_item.back().second.assign(content);

            ++comment_buffer_size_;
            if (IsCommentBufferFull_())
                FlushCommentBuffer_();
        }
    }
    FlushCommentBuffer_();

    CommentCacheStorage::CommentCacheIteratorType commentCacheIt(comment_cache_storage_->comment_cache_db_);
    CommentCacheStorage::CommentCacheIteratorType commentCacheEnd;
    for (; commentCacheIt != commentCacheEnd; ++commentCacheIt)
    {
        const UString& key = commentCacheIt->first;
        const CommentBufferItemType& cache_item = commentCacheIt->second;
        DoEvaluateSummarization_(key, cache_item);
    }
    summarization_storage_->Flush();
}

void MultiDocSummarizationSubManager::FlushCommentBuffer_()
{
    if (comment_buffer_.empty()) return;

    for (CommentBufferType::iterator it = comment_buffer_.begin();
            it != comment_buffer_.end(); ++it)
    {
        CommentBufferItemType value;
        if (comment_cache_storage_->Get(it->first, value))
        {
            for (uint32_t i = 0; i < it->second.size(); i++)
            {
                value.push_back(std::make_pair(it->second[i].first, UString()));
                value.back().second.swap(it->second[i].second);
            }
        }
        else
        {
            value.swap(it->second);
        }
        comment_cache_storage_->Update(it->first, value);
    }
    comment_cache_storage_->Flush();

    comment_buffer_.clear();
    comment_buffer_size_ = 0;
}

void MultiDocSummarizationSubManager::DoEvaluateSummarization_(
        const UString& key,
        const CommentBufferItemType& comment_buffer_item)
{
    Summarization summarization;
    for (uint32_t i = 0; i < comment_buffer_item.size(); i++)
    {
        summarization.insertDoc(comment_buffer_item[i].first);
    }
    if (!summarization_storage_->IsRebuildSummarizeRequired(key, summarization))
        return;

    ilplib::langid::Analyzer* langIdAnalyzer = document_manager_->getLangId();

    corpus_->start_new_coll(key);
    for (CommentBufferItemType::const_iterator it = comment_buffer_item.begin();
            it != comment_buffer_item.end(); ++it)
    {
//      Document doc;
//      document_manager_->getDocument(docs[i], doc);
//      Document::property_const_iterator it = doc.findProperty(schema_.contentPropName);
//      if (it == doc.propertyEnd())
//          continue;

        corpus_->start_new_doc();

        const UString& content = it->second;
        UString sentence;
        std::size_t startPos = 0;
        while (std::size_t len = langIdAnalyzer->sentenceLength(content, startPos))
        {
            sentence.assign(content, startPos, len);

            corpus_->start_new_sent(sentence);

            std::vector<UString> word_list;
            analyzer_->GetStringList(sentence, word_list);
            for (std::vector<UString>::const_iterator wit = word_list.begin();
                    wit != word_list.end(); ++it)
            {
                corpus_->add_word(*wit);
            }

            startPos += len;
        }
    }
    corpus_->start_new_sent();
    corpus_->start_new_doc();
    corpus_->start_new_coll();

    std::vector<std::pair<UString, std::vector<UString> > > summary_list;
//  std::string key_str;
//  key.convertString(key_str, UString::UTF_8);
//  std::cout << "Begin evaluating: " << key_str << std::endl;
    if (comment_buffer_item.size() < 400 && corpus_->ntotal() < 5000)
    {
        SPLM::generateSummary(summary_list, *corpus_, SPLM::SPLM_SVD);
    }
    else if (comment_buffer_item.size() < 800 && corpus_->ntotal() < 10000)
    {
        SPLM::generateSummary(summary_list, *corpus_, SPLM::SPLM_RI);
    }
    else
    {
        SPLM::generateSummary(summary_list, *corpus_, SPLM::SPLM_NONE);
    }
//  std::cout << "End evaluating: " << key_str << std::endl;

    //XXX store the generated summary list
    std::vector<UString>& summary = summary_list[0].second;
    if (!summary.empty())
    {
//      for (uint32_t i = 0; i < summary.size(); i++)
//      {
//          std::string sent;
//          summary[i].convertString(sent, UString::UTF_8);
//          std::cout << "\t" << sent << std::endl;
//      }
        summarization.property("overview").swap(summary);
    }
    summarization_storage_->Update(key, summarization);

    corpus_->reset();
}

bool MultiDocSummarizationSubManager::GetSummarizationByRawKey(
        const UString& rawKey,
        Summarization& result)
{
    return summarization_storage_->Get(rawKey, result);
}

void MultiDocSummarizationSubManager::AppendSearchFilter(
        std::vector<QueryFiltering::FilteringType>& filtingList)
{
    ///When search filter is based on ParentKey, get its associated values,
    ///and add those values to filter conditions.
    ///The typical situation of this happen when :
    ///SELECT * FROM comments WHERE product_type="foo"
    ///This hook will translate the semantic into:
    ///SELECT * FROM comments WHERE product_id="1" OR product_id="2" ...

    typedef std::vector<QueryFiltering::FilteringType>::iterator IteratorType;
    IteratorType it = std::find_if(filtingList.begin(),
            filtingList.end(), IsParentKeyFilterProperty(schema_.parentKey));
    if (it != filtingList.end())
    {
        const std::vector<PropertyValue>& filterParam = it->second;
        if (!filterParam.empty())
        {
            try
            {
                const std::string& paramValue = get<std::string>(filterParam[0]);
                UString paramUStr(paramValue, UString::UTF_8);
                std::vector<UString> results;
                if (parent_key_storage_->Get(paramUStr, results))
                {
                    BTreeIndexerManager* pBTreeIndexer = index_manager_->getBTreeIndexer();
                    QueryFiltering::FilteringType filterRule;
                    filterRule.first.first = QueryFiltering::INCLUDE;
                    filterRule.first.second = schema_.foreignKeyPropName;
                    std::vector<UString>::const_iterator rit = results.begin();
                    for (; rit != results.end(); ++rit)
                    {
                        if(pBTreeIndexer->seek(schema_.foreignKeyPropName, *rit))
                        {
                            ///Protection
                            ///Or else, too many unexisted keys are added
                            PropertyValue v(*rit);
                            filterRule.second.push_back(v);
                        }
                    }
                    filtingList.erase(it);
                    filtingList.push_back(filterRule);
                }
            }
            catch (const boost::bad_get &)
            {
                filtingList.erase(it);
                return;
            }
        }
    }
}

void MultiDocSummarizationSubManager::BuildIndexOfParentKey_()
{
    if (schema_.parentKeyLogPath.empty()) return;
    ScdParser parser(UString::UTF_8);
    std::vector<std::string> scdList;
    static const bfs::directory_iterator kItrEnd;
    for (bfs::directory_iterator itr(schema_.parentKeyLogPath); itr != kItrEnd; ++itr)
    {
        if (bfs::is_regular_file(itr->status()))
        {
            std::string fileName = itr->path().filename();
            if (parser.checkSCDFormat(fileName))
            {
                scdList.push_back(itr->path().string());
            }
            else
            {
                LOG(WARNING) << "SCD File not valid " << fileName;
            }
        }
    }

    std::vector<std::string>::const_iterator scd_it = scdList.begin();

    for (; scd_it != scdList.end(); ++scd_it)
    {
        size_t pos = scd_it->rfind("/") + 1;
        string filename = scd_it->substr(pos);

        LOG(INFO) << "Processing SCD file. " << bfs::path(*scd_it).stem();

        switch (parser.checkSCDType(*scd_it))
        {
        case INSERT_SCD:
            DoInsertBuildIndexOfParentKey_(*scd_it);
            LOG(INFO) << "Indexing Finished";
            break;
        case DELETE_SCD:
            DoDelBuildIndexOfParentKey_(*scd_it);
            LOG(INFO) << "Delete Finished";
            break;
        case UPDATE_SCD:
            DoUpdateIndexOfParentKey_(*scd_it);
            LOG(INFO) << "Update Finished";
            break;
        default:
            break;
        }
        parser.load(*scd_it);
    }
    parent_key_storage_->Flush();

    bfs::path bkDir = bfs::path(schema_.parentKeyLogPath) / "backup";
    bfs::create_directory(bkDir);
    LOG(INFO) << "moving " << scdList.size() << " SCD files to directory " << bkDir;

    for (scd_it = scdList.begin(); scd_it != scdList.end(); ++scd_it)
    {
        try
        {
            bfs::rename(*scd_it, bkDir / bfs::path(*scd_it).filename());
        }
        catch (bfs::filesystem_error& e)
        {
            LOG(WARNING) << "exception in rename file " << *scd_it << ": " << e.what();
        }
    }
}

void MultiDocSummarizationSubManager::DoInsertBuildIndexOfParentKey_(
        const std::string& fileName)
{
    ScdParser parser(UString::UTF_8);
    if (!parser.load(fileName)) return;
    for (ScdParser::iterator doc_iter = parser.begin();
            doc_iter != parser.end(); ++doc_iter)
    {
        if (*doc_iter == NULL)
        {
            LOG(WARNING) << "SCD File not valid.";
            return;
        }
        SCDDocPtr doc = (*doc_iter);
        if (!CheckParentKeyLogFormat(doc, parent_key_ustr_name_))
            continue;
        parent_key_storage_->AppendUpdate((*doc)[1].second, (*doc)[0].second);
    }
}

void MultiDocSummarizationSubManager::DoUpdateIndexOfParentKey_(
        const std::string& fileName)
{
}

void MultiDocSummarizationSubManager::DoDelBuildIndexOfParentKey_(
        const std::string& fileName)
{
}

}
