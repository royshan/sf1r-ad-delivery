#include "PriceHistory.h"

#include <common/Utilities.h>
#include <libcassandra/cassandra.h>
#include <libcassandra/util_functions.h>

#include <boost/assign/list_of.hpp>

using namespace std;
using namespace boost::assign;
using namespace libcassandra;
using namespace org::apache::cassandra;

namespace sf1r {

const string PriceHistory::cf_name("PriceHistory");

const string PriceHistory::cf_column_type;

const string PriceHistory::cf_comparator_type("LongType");

const string PriceHistory::cf_sub_comparator_type;

const string PriceHistory::cf_comment(
    "\n\nThis column family stores recent two years price history for each product.\n"
    "Schema:\n\n"
    "    column family PriceHistory = list of {\n"
    "        key \"docid\" : list of {\n"
    "            name \"timestamp\" : value \"price\"\n"
    "        }\n"
    "    }\n\n");

const double PriceHistory::cf_row_cache_size(0);

const double PriceHistory::cf_key_cache_size(0);

const double PriceHistory::cf_read_repair_chance(0);

const vector<ColumnDef> PriceHistory::cf_column_metadata;

const int32_t PriceHistory::cf_gc_grace_seconds(0);

const string PriceHistory::cf_default_validation_class;

const int32_t PriceHistory::cf_id(0);

const int32_t PriceHistory::cf_min_compaction_threshold(0);

const int32_t PriceHistory::cf_max_compaction_threshold(0);

const int32_t PriceHistory::cf_row_cache_save_period_in_seconds(0);

const int32_t PriceHistory::cf_key_cache_save_period_in_seconds(0);

const int8_t PriceHistory::cf_replicate_on_write(-1);

const double PriceHistory::cf_merge_shards_chance(0);

const string PriceHistory::cf_key_validation_class("BytesType");

const string PriceHistory::cf_row_cache_provider("SerializingCacheProvider");

const string PriceHistory::cf_key_alias;

const string PriceHistory::cf_compaction_strategy("LeveledCompactionStrategy");

const map<string, string> PriceHistory::cf_compaction_strategy_options;

const int32_t PriceHistory::cf_row_cache_keys_to_save(0);

const map<string, string> PriceHistory::cf_compression_options = map_list_of
    ("sstable_compression", "SnappyCompressor")
    ("chunk_length_kb", "64");

const double PriceHistory::cf_bloom_filter_fp_chance(0);

PriceHistoryRow::PriceHistoryRow(const uint128_t& docId)
    : docId_(docId)
    , priceHistoryPresent_(false)
{
}

PriceHistoryRow::~PriceHistoryRow()
{
}

bool PriceHistoryRow::insert(const string& name, const string& value)
{
    clear();
    if (value.length() != sizeof(ProductPrice))
    {
        cerr << "Bad insert!" << endl;
        return false;
    }
    const time_t& timestamp = deserializeLong(name);
    if (timestamp > 0) // XXX should be deleted after Cassandra migration
        priceHistory_.push_back(std::make_pair(timestamp, Utilities::fromBytes<ProductPrice>(value)));
    return true;
}

void PriceHistoryRow::insert(time_t timestamp, ProductPrice price)
{
    clear();
    if (timestamp > 0) // XXX should be deleted after Cassandra migration
        priceHistory_.push_back(std::make_pair(timestamp, price));
}

void PriceHistoryRow::resetHistory(uint32_t index, time_t timestamp, ProductPrice price)
{
    clear();
    if (priceHistory_.size() <= index)
        priceHistory_.resize(index + 1);
    priceHistory_[index].first = timestamp;
    priceHistory_[index].second = price;
}

void PriceHistoryRow::resetKey(const uint128_t& newDocId)
{
    docId_ = newDocId;
    if (newDocId == 0)
    {
        priceHistoryPresent_ = false;
    }
}

void PriceHistoryRow::clear()
{
    if (!priceHistoryPresent_)
    {
        priceHistory_.clear();
        priceHistoryPresent_ = true;
    }
}

PriceHistory::PriceHistory(const string& keyspace_name)
    : is_enabled(false)
    , keyspace_name_(keyspace_name)
{
}

PriceHistory::~PriceHistory()
{
}

bool PriceHistory::updateMultiRow(const vector<PriceHistoryRow>& row_list)
{
    if (row_list.empty()) return true;
    if (!is_enabled) return false;
    try
    {
        map<string, map<string, vector<Mutation> > > mutation_map;
        time_t timestamp = Utilities::createTimeStamp();
        for (vector<PriceHistoryRow>::const_iterator vit = row_list.begin();
                vit != row_list.end(); ++vit)
        {
            vector<Mutation>& mutation_list = mutation_map[Utilities::toBytes(vit->docId_)][cf_name];
            for (PriceHistoryType::const_iterator pit = vit->priceHistory_.begin();
                    pit != vit->priceHistory_.end(); ++pit)
            {
                mutation_list.push_back(Mutation());
                Mutation& mut = mutation_list.back();
                mut.__isset.column_or_supercolumn = true;
                mut.column_or_supercolumn.__isset.column = true;
                Column& col = mut.column_or_supercolumn.column;
                col.__set_name(serializeLong(pit->first));
                col.__set_value(Utilities::toBytes(pit->second));
                col.__set_timestamp(timestamp);
                col.__set_ttl(63072000);
            }
        }

        CassandraConnection::instance().getCassandraClient(keyspace_name_)->batchMutate(mutation_map);
    }
    CATCH_CASSANDRA_EXCEPTION("[CassandraConnection] error:");

    return true;
}

bool PriceHistory::getMultiSlice(
        vector<PriceHistoryRow>& row_list,
        const vector<uint128_t>& key_list,
        const string& start,
        const string& finish,
        int32_t count,
        bool reversed)
{
    if (!is_enabled) return false;
    try
    {
        ColumnParent col_parent;
        col_parent.__set_column_family(cf_name);

        SlicePredicate pred;
        pred.__isset.slice_range = true;
        pred.slice_range.__set_start(start);
        pred.slice_range.__set_finish(finish);
        pred.slice_range.__set_count(count);
        pred.slice_range.__set_reversed(reversed);

        vector<string> str_key_list;
        str_key_list.reserve(key_list.size());
        for (uint32_t i = 0; i < key_list.size(); i++)
        {
            str_key_list.push_back(Utilities::toBytes(key_list[i]));
        }

        map<string, vector<ColumnOrSuperColumn> > raw_column_map;
        CassandraConnection::instance().getCassandraClient(keyspace_name_)->getMultiSlice(
                raw_column_map,
                str_key_list,
                col_parent,
                pred);

        row_list.reserve(row_list.size() + raw_column_map.size());
        for (map<string, vector<ColumnOrSuperColumn> >::const_iterator mit = raw_column_map.begin();
                mit != raw_column_map.end(); ++mit)
        {
            if (mit->second.empty()) continue;
            row_list.push_back(PriceHistoryRow(Utilities::fromBytes<uint128_t>(mit->first)));
            PriceHistoryRow& price_history = row_list.back();
            for (vector<ColumnOrSuperColumn>::const_iterator vit = mit->second.begin();
                    vit != mit->second.end(); ++vit)
            {
                price_history.insert(vit->column.name, vit->column.value);
            }
        }
    }
    CATCH_CASSANDRA_EXCEPTION("[CassandraConnection] error:");

    return true;
}

bool PriceHistory::getMultiCount(
        std::vector<std::pair<uint128_t, int32_t> >& count_list,
        const vector<uint128_t>& key_list,
        const string& start,
        const string& finish)
{
    if (!is_enabled) return false;
    try
    {
        ColumnParent col_parent;
        col_parent.__set_column_family(cf_name);

        SlicePredicate pred;
        pred.__isset.slice_range = true;
        pred.slice_range.__set_start(start);
        pred.slice_range.__set_finish(finish);
        //pred.slice_range.__set_count(numeric_limits<int32_t>::max());

        vector<string> str_key_list;
        str_key_list.reserve(key_list.size());
        for (uint32_t i = 0; i < key_list.size(); i++)
        {
            str_key_list.push_back(Utilities::toBytes(key_list[i]));
        }

        map<string, int32_t> count_map;
        CassandraConnection::instance().getCassandraClient(keyspace_name_)->getMultiCount(
                count_map,
                str_key_list,
                col_parent,
                pred);

        count_list.resize(count_map.size());
        uint32_t i = 0;
        for (map<string, int32_t>::const_iterator it = count_map.begin();
                it != count_map.end(); ++it)
        {
            count_list[i].first = Utilities::fromBytes<uint128_t>(it->first);
            count_list[i++].second = it->second;
        }
    }
    CATCH_CASSANDRA_EXCEPTION("[CassandraConnection] error:");

    return true;
}

bool PriceHistory::updateRow(const PriceHistoryRow& row) const
{
    if (!is_enabled || row.docId_ == 0) return false;
    if (!row.priceHistoryPresent_) return true;
    try
    {
        map<string, map<string, vector<Mutation> > > mutation_map;
        time_t timestamp = Utilities::createTimeStamp();
        vector<Mutation>& mutation_list = mutation_map[Utilities::toBytes(row.docId_)][cf_name];

        for (PriceHistoryType::const_iterator pit = row.priceHistory_.begin();
                pit != row.priceHistory_.end(); ++pit)
        {
            mutation_list.push_back(Mutation());
            Mutation& mut = mutation_list.back();
            mut.__isset.column_or_supercolumn = true;
            mut.column_or_supercolumn.__isset.column = true;
            Column& col = mut.column_or_supercolumn.column;
            col.__set_name(serializeLong(pit->first));
            col.__set_value(Utilities::toBytes(pit->second));
            col.__set_timestamp(timestamp);
            col.__set_ttl(63072000);
        }

        CassandraConnection::instance().getCassandraClient(keyspace_name_)->batchMutate(mutation_map);
    }
    CATCH_CASSANDRA_EXCEPTION("[CassandraConnection] error:");

    return true;
}

void PriceHistory::createColumnFamily()
{
    is_enabled = CassandraConnection::instance().createColumnFamily(
            keyspace_name_,
            cf_name,
            cf_column_type,
            cf_comparator_type,
            cf_sub_comparator_type,
            cf_comment,
            cf_row_cache_size,
            cf_key_cache_size,
            cf_read_repair_chance,
            cf_column_metadata,
            cf_gc_grace_seconds,
            cf_default_validation_class,
            cf_id,
            cf_min_compaction_threshold,
            cf_max_compaction_threshold,
            cf_row_cache_save_period_in_seconds,
            cf_key_cache_save_period_in_seconds,
            cf_replicate_on_write,
            cf_merge_shards_chance,
            cf_key_validation_class,
            cf_row_cache_provider,
            cf_key_alias,
            cf_compaction_strategy,
            cf_compaction_strategy_options,
            cf_row_cache_keys_to_save,
            cf_compression_options,
            cf_bloom_filter_fp_chance);
}

bool PriceHistory::truncateColumnFamily() const
{
    if (!is_enabled) return false;
    try
    {
        CassandraConnection::instance().getCassandraClient(keyspace_name_)->truncateColumnFamily(cf_name);
    }
    CATCH_CASSANDRA_EXCEPTION("[CassandraConnection] error:");

    return true;
}

bool PriceHistory::dropColumnFamily() const
{
    if (!is_enabled) return false;
    try
    {
        CassandraConnection::instance().getCassandraClient(keyspace_name_)->dropColumnFamily(cf_name);
    }
    CATCH_CASSANDRA_EXCEPTION("[CassandraConnection] error:");

    return true;
}

bool PriceHistory::getSlice(PriceHistoryRow& row, const string& start, const string& finish, int32_t count, bool reversed)
{
    if (!is_enabled || row.docId_ == 0) return false;
    try
    {
        ColumnParent col_parent;
        col_parent.__set_column_family(cf_name);

        SlicePredicate pred;
        pred.__isset.slice_range = true;
        pred.slice_range.__set_start(start);
        pred.slice_range.__set_finish(finish);
        pred.slice_range.__set_count(count);
        pred.slice_range.__set_reversed(reversed);

        vector<ColumnOrSuperColumn> raw_column_list;
        CassandraConnection::instance().getCassandraClient(keyspace_name_)->getRawSlice(
                raw_column_list,
                Utilities::toBytes(row.docId_),
                col_parent,
                pred);
        if (raw_column_list.empty()) return true;

        row.clear();
        for (vector<ColumnOrSuperColumn>::const_iterator it = raw_column_list.begin();
                it != raw_column_list.end(); ++it)
        {
            row.insert(it->column.name, it->column.value);
        }
    }
    CATCH_CASSANDRA_EXCEPTION("[CassandraConnection] error:");

    return true;
}

bool PriceHistory::deleteRow(const std::string& key)
{
    if (!is_enabled || key.empty()) return false;
    try
    {
        ColumnPath col_path;
        col_path.__set_column_family(cf_name);
        CassandraConnection::instance().getCassandraClient(keyspace_name_)->remove(
                key,
                col_path);
    }
    CATCH_CASSANDRA_EXCEPTION("[CassandraConnection] error:");

    return true;
}

bool PriceHistory::getCount(int32_t& count, const string& key, const string& start, const string& finish) const
{
    if (!is_enabled || key.empty()) return false;
    try
    {
        ColumnParent col_parent;
        col_parent.__set_column_family(cf_name);

        SlicePredicate pred;
        pred.__isset.slice_range = true;
        pred.slice_range.__set_start(start);
        pred.slice_range.__set_finish(finish);
        //pred.slice_range.__set_count(numeric_limits<int32_t>::max());

        count = CassandraConnection::instance().getCassandraClient(keyspace_name_)->getCount(
                key,
                col_parent,
                pred);
    }
    CATCH_CASSANDRA_EXCEPTION("[CassandraConnection] error:");

    return true;
}

}
