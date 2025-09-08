#pragma once

#include <string>
#include <vector>

namespace ProjectIE4k {

// Minimal, robust 2DA text parser inspired by GemRB's importer
class TwoDATable {
public:
    // Parse from whole-file text. Returns false on gross errors; tolerant otherwise.
    bool loadFromText(const std::string& text);

    // Serialize back to text. Formatting is normalized (tab-separated columns).
    std::string serializeToText() const;

    // Queries
    using index_t = int;
    static const index_t npos;

    index_t getRowCount() const { return static_cast<index_t>(rows_.size()); }
    index_t getColNamesCount() const { return static_cast<index_t>(colNames_.size()); }
    index_t getColumnCount(index_t row) const;

    const std::string& queryField(index_t row, index_t column) const;
    const std::string& queryDefault() const { return defVal_; }

    index_t getRowIndex(const std::string& key) const;
    index_t getColumnIndex(const std::string& key) const;
    const std::string& getColumnName(index_t index) const;
    const std::string& getRowName(index_t index) const;

    // Transformations
    // Scales all integer cells in rows whose name starts with any of the prefixes.
    void scaleRowsByPrefixes(const std::vector<std::string>& rowPrefixes, int factor);
    // Scales integer cells in a specific data column (zero-based, excluding row-name) across all rows by a floating factor (rounded).
    void scaleIntegerColumnBy(index_t column, double factor);

private:
    static bool iequals(const std::string& a, const std::string& b);
    static bool istarts_with(const std::string& s, const std::string& prefix);
    static bool isInteger(const std::string& s);
    static std::vector<std::string> splitWS(const std::string& line);
    static std::string ltrim(const std::string& s);

    std::string signature_;
    std::string defVal_;
    std::vector<std::string> colNames_;
    std::vector<std::string> rowNames_;
    std::vector<std::vector<std::string>> rows_;
};

} // namespace ProjectIE4k


