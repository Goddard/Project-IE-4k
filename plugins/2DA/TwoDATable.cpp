#include "TwoDATable.h"

#include <cctype>
#include <sstream>

namespace ProjectIE4k {

const TwoDATable::index_t TwoDATable::npos = -1;

static bool str_ieq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i=0;i<a.size();++i) if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}

bool TwoDATable::iequals(const std::string& a, const std::string& b) { return str_ieq(a,b); }

bool TwoDATable::istarts_with(const std::string& s, const std::string& prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i=0;i<prefix.size();++i) if (std::tolower((unsigned char)s[i]) != std::tolower((unsigned char)prefix[i])) return false;
    return true;
}

std::string TwoDATable::ltrim(const std::string& s) {
    size_t i=0; while (i<s.size() && std::isspace((unsigned char)s[i])) ++i; return s.substr(i);
}

std::vector<std::string> TwoDATable::splitWS(const std::string& line) {
    std::vector<std::string> out; std::string cur; bool in=false;
    for (char ch : line) {
        if (std::isspace((unsigned char)ch)) { if (in){ out.push_back(cur); cur.clear(); in=false; } }
        else { in=true; cur.push_back(ch);} }
    if (in) out.push_back(cur);
    return out;
}

bool TwoDATable::isInteger(const std::string& s) {
    if (s.empty()) return false; size_t i=0; if (s[0]=='+'||s[0]=='-') i=1; if (i>=s.size()) return false;
    for (; i<s.size(); ++i) if (!std::isdigit((unsigned char)s[i])) return false; return true;
}

bool TwoDATable::loadFromText(const std::string& text) {
    signature_.clear(); defVal_.clear(); colNames_.clear(); rowNames_.clear(); rows_.clear();
    std::istringstream ss(text);
    std::string line;
    // signature
    if (!std::getline(ss, line)) return false;
    signature_ = line;
    // default value
    if (!std::getline(ss, line)) return false;
    {
        auto pos = line.find_first_of(' ');
        if (pos != std::string::npos) defVal_ = line.substr(0, pos); else defVal_ = line;
    }
    // next line: column headings (optional)
    auto NextLine = [&](std::string& out)->bool{
        while (std::getline(ss, out)) { if (out.empty() || out[0] == '#') continue; return true; } return false; };

    if (!NextLine(line)) return true; // empty table after default
    {
        size_t end = line.find_first_not_of(" \t\r\n");
        if (end != std::string::npos) {
            colNames_ = splitWS(line.substr(end));
        }
    }

    // data rows
    while (NextLine(line)) {
        auto pos = line.find_first_of(' ');
        if (pos == std::string::npos) {
            if (line.empty()) continue;
            rowNames_.push_back(line);
            rows_.emplace_back();
            continue;
        }
        rowNames_.push_back(line.substr(0, pos));
        std::string rest = line.substr(pos+1);
        auto row = splitWS(rest);
        if (!row.empty() && row.back().empty()) row.pop_back();
        rows_.push_back(std::move(row));
    }
    return true;
}

std::string TwoDATable::serializeToText() const {
    std::ostringstream out;
    out << signature_ << "\n";
    out << defVal_ << "\n";

    // Compute widths for aligned, space-separated formatting
    size_t rowNameWidth = 0;
    for (const auto &rn : rowNames_) if (rn.size() > rowNameWidth) rowNameWidth = rn.size();
    size_t numCols = colNames_.size();
    for (const auto &row : rows_) if (row.size() > numCols) numCols = row.size();
    std::vector<size_t> colWidths(numCols, 0);
    for (size_t c = 0; c < numCols; ++c) {
        if (c < colNames_.size() && colNames_[c].size() > colWidths[c]) colWidths[c] = colNames_[c].size();
        for (const auto &row : rows_) {
            if (c < row.size() && row[c].size() > colWidths[c]) colWidths[c] = row[c].size();
        }
    }

    auto appendPaddedRight = [](std::ostringstream &o, const std::string &s, size_t width) {
        if (s.size() < width) o << std::string(width - s.size(), ' ');
        o << s;
    };
    auto appendPaddedLeft = [](std::ostringstream &o, const std::string &s, size_t width) {
        o << s;
        if (s.size() < width) o << std::string(width - s.size(), ' ');
    };

    // Header line (if any): indent to row name column, then right-align headers per column width
    if (!colNames_.empty()) {
        out << std::string(rowNameWidth + 2, ' ');
        for (size_t c = 0; c < numCols; ++c) {
            if (c > 0) out << std::string(2, ' ');
            const std::string &h = (c < colNames_.size() ? colNames_[c] : std::string());
            appendPaddedRight(out, h, colWidths[c]);
        }
        out << "\n";
    }

    // Data rows: left-align row name to rowNameWidth, then right-align each cell
    for (size_t r = 0; r < rows_.size(); ++r) {
        appendPaddedLeft(out, rowNames_[r], rowNameWidth);
        if (numCols > 0) out << std::string(2, ' ');
        for (size_t c = 0; c < numCols; ++c) {
            if (c > 0) out << std::string(2, ' ');
            const std::string &cell = (c < rows_[r].size() ? rows_[r][c] : std::string());
            appendPaddedRight(out, cell, colWidths[c]);
        }
        if (r + 1 < rows_.size()) out << '\n';
    }
    return out.str();
}

TwoDATable::index_t TwoDATable::getColumnCount(index_t row) const {
    if (row < 0 || static_cast<size_t>(row) >= rows_.size()) return 0; return static_cast<index_t>(rows_[row].size());
}

const std::string& TwoDATable::queryField(index_t row, index_t column) const {
    static const std::string empty;
    if (row < 0 || static_cast<size_t>(row) >= rows_.size()) return defVal_;
    if (column < 0 || static_cast<size_t>(column) >= rows_[row].size()) return defVal_;
    if (rows_[row][column] == "*") return defVal_;
    return rows_[row][column];
}

TwoDATable::index_t TwoDATable::getRowIndex(const std::string& key) const {
    for (index_t i=0; i<(index_t)rowNames_.size(); ++i) if (iequals(rowNames_[i], key)) return i; return npos;
}

TwoDATable::index_t TwoDATable::getColumnIndex(const std::string& key) const {
    for (index_t i=0; i<(index_t)colNames_.size(); ++i) if (iequals(colNames_[i], key)) return i; return npos;
}

const std::string& TwoDATable::getColumnName(index_t index) const {
    static const std::string blank;
    if (index >= 0 && (size_t)index < colNames_.size()) return colNames_[index];
    return blank;
}

const std::string& TwoDATable::getRowName(index_t index) const {
    static const std::string blank;
    if (index >= 0 && (size_t)index < rowNames_.size()) return rowNames_[index];
    return blank;
}

void TwoDATable::scaleRowsByPrefixes(const std::vector<std::string>& rowPrefixes, int factor) {
    if (factor == 1) return;
    for (size_t r=0; r<rows_.size(); ++r) {
        const std::string& name = rowNames_[r];
        bool match = false;
        for (const auto& p : rowPrefixes) { if (istarts_with(name, p)) { match = true; break; } }
        if (!match) continue;
        for (auto& cell : rows_[r]) {
            if (isInteger(cell)) {
                long v = std::stol(cell);
                v *= factor;
                cell = std::to_string(v);
            }
        }
    }
}

void TwoDATable::scaleIntegerColumnBy(index_t column, double factor) {
    if (column < 0) return;
    for (auto &row : rows_) {
        if (static_cast<size_t>(column) < row.size()) {
            std::string &cell = row[static_cast<size_t>(column)];
            if (isInteger(cell)) {
                long v = std::stol(cell);
                long nv = static_cast<long>(v * factor + (factor >= 0 ? 0.5 : -0.5));
                cell = std::to_string(nv);
            }
        }
    }
}

} // namespace ProjectIE4k


