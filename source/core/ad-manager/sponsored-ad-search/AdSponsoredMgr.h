#ifndef AD_SPONSORED_MGR_H
#define AD_SPONSORED_MGR_H

#include "AdCommonDataType.h"
#include <ir/id_manager/IDManager.h>

#include "AdManualBidInfoMgr.h"
#include <vector>
#include <map>
#include <deque>
#include <boost/unordered_map.hpp>
#include <boost/shared_ptr.hpp>
#include <string>
#include <set>

namespace sf1r
{

class TitlePCAWrapper;
class SearchKeywordOperation;
class KeywordSearchResult;
class AdKeywordSearchResult;
class DocumentManager;
class AdSearchService;
namespace faceted
{
    class GroupManager;
}

namespace sponsored
{

class AdAuctionLogMgr;
class AdBidStrategy;
class AdQueryStatisticInfo;
// note: all the bid price , budget is in unit of 0.01 yuan.
class AdSponsoredMgr
{
public:
    enum kBidStrategy
    {
        UniformBid,
        GeneticBid,
        RealtimeBid,
        Unknown
    };

    AdSponsoredMgr();
    ~AdSponsoredMgr();
    void init(const std::string& dict_path,
        const std::string& data_path,
        const std::string& commondata_path,
        faceted::GroupManager* grp_mgr,
        DocumentManager* doc_mgr,
        izenelib::ir::idmanager::IDManager* id_manager,
        AdSearchService* searcher);

    void miningAdCreatives(ad_docid_t start_id, ad_docid_t end_id);
    void tokenize(const std::string& str, std::vector<std::string>& tokens);
    void generateBidPhrase(const std::string& ad_title, BidPhraseT& bidphrase_list);
    void generateBidPrice(ad_docid_t adid, std::vector<int>& price_list);

    bool sponsoredAdSearch(const SearchKeywordOperation& actionOperation,
        KeywordSearchResult& searchResult);

    void getAdBidPrice(ad_docid_t adid, const std::string& query, int leftbudget, int& price);
    int getBudgetLeft(ad_docid_t adid);
    void getBidPhrase(const std::string& adid, BidPhraseT& bidphrase);
    bool getAdIdFromAdStrId(const std::string& strid, ad_docid_t& adid);
    bool getAdStrIdFromAdId(ad_docid_t adid, std::string& ad_strid);

    void updateAuctionLogData(const std::string& ad_id,
        int click_cost_in_fen, uint32_t click_slot);

    void save();
    void changeDailyBudget(const std::string& ad_campaign_name, int dailybudget);

    void setManualBidPrice(const std::string& campaign_name,
        const std::vector<std::string>& key_list,
        const std::vector<int>& price_list);

    void resetDailyLogStatisticalData(bool reset_used);
    inline double getAdCTR(ad_docid_t adid)
    {
        if (adid >= ad_ctr_list_.size())
            return 0;
        return ad_ctr_list_[adid];
    }


private:
    typedef boost::unordered_map<std::string, uint32_t>  StrIdMapT;
    typedef std::vector<std::pair<int, double> > BidAuctionLandscapeT;
    typedef std::string LogBidKeywordId;

    double computeAdCTR(ad_docid_t adid);
    void updateAdBidPhrase(ad_docid_t adid, const std::vector<std::string>& bid_phrase_list,
        BidPhraseT& bidid_list);
    void updateAdCampaign(ad_docid_t adid, const std::string& campaign_name);

    void generateBidPhrase(const std::string& ad_title, std::vector<std::string>& bidphrase);
    inline double getAdRelevantScore(const BidPhraseT& bidphrase, const BidPhraseT& query_kid_list)
    {
        return bidphrase.size(); 
    }
    inline double getAdQualityScore(ad_docid_t adid, const BidPhraseT& bidphrase, const BidPhraseT& query_kid_list)
    {
        return getAdCTR(adid) * getAdRelevantScore(bidphrase, query_kid_list);
    }
    void consumeBudget(ad_docid_t adid, int cost);
    void load();
    bool getBidKeywordId(const std::string& keyword, bool insert, BidKeywordId& id);
    void getLogBidKeywordId(const BidKeywordId& id, LogBidKeywordId& keyword);
    void getBidPhrase(const std::string& adid, BidPhraseT& bidphrase, std::vector<LogBidKeywordId>& logbid_list);

    void getBidStatisticalData(const std::set<BidKeywordId>& bidkey_list,
        const std::map<LogBidKeywordId, BidAuctionLandscapeT>& bidkey_cpc_map,
        const std::map<std::string, int>& manual_bidprice_list,
        std::vector<AdQueryStatisticInfo>& ad_statistical_data);

    std::string data_path_;
    // all bid phrase for all ad creatives.
    std::vector<BidPhraseT>  ad_bidphrase_list_;
    std::vector<double>  ad_ctr_list_;
    std::vector<std::string> keyword_id_value_list_;
    StrIdMapT keyword_value_id_list_;

    // the used budget for specific ad campaign. update realtime.
    std::vector<int> ad_budget_used_list_;
    std::vector<std::string>  ad_campaign_name_list_;
    StrIdMapT ad_campaign_name_id_list_;
    std::vector<uint32_t>  ad_campaign_belong_list_; 
    std::vector<std::set<BidKeywordId> >  ad_campaign_bid_keyword_list_;

    faceted::GroupManager* grp_mgr_;
    DocumentManager* doc_mgr_;
    izenelib::ir::idmanager::IDManager* id_manager_;
    AdSearchService* ad_searcher_;

    boost::shared_ptr<AdAuctionLogMgr> ad_log_mgr_;
    boost::shared_ptr<AdBidStrategy> ad_bid_strategy_;
    kBidStrategy bid_strategy_type_;
    // bid price for different campaign.
    std::vector<std::map<BidKeywordId, int> > ad_bid_price_list_;
    std::vector<std::vector<std::pair<int, double> > > ad_uniform_bid_price_list_;

    AdManualBidInfoMgr manual_bidinfo_mgr_;
};

}
}

#endif
