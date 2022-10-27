#include "stddef.h"
#include "clang-c/Index.h" // This is libclang.
#include <iostream>
#include <sys/stat.h>
#include <vector>

using namespace std;
#pragma comment(lib, "clang-c/libclang.lib")

enum Requirement
{
    Basic = 1,
    Advanced,
    Uplifting,
    Ultimate
};

ostream &operator<<(ostream &stream, const CXString &str)
{
    stream << clang_getCString(str);
    clang_disposeString(str);
    return stream;
}

unsigned getFilesize(const char *fileName)
{
    struct stat statbuf;
    stat(fileName, &statbuf);
    return statbuf.st_size;
}

CXSourceRange getFilerange(const CXTranslationUnit &tu, const char *filename)
{
    CXFile file = clang_getFile(tu, filename);
    auto fileSize = getFilesize(filename);

    // get top/last location of the file
    CXSourceLocation topLoc = clang_getLocationForOffset(tu, file, 0);
    CXSourceLocation lastLoc = clang_getLocationForOffset(tu, file, fileSize);
    if (clang_equalLocations(topLoc, clang_getNullLocation()) ||
        clang_equalLocations(lastLoc, clang_getNullLocation()))
    {
        printf("cannot retrieve location\n");
        exit(1);
    }

    // make a range from locations
    CXSourceRange range = clang_getRange(topLoc, lastLoc);
    if (clang_Range_isNull(range))
    {
        printf("cannot retrieve range\n");
        exit(1);
    }

    return range;
}

const char *_getTokenKindSpelling(CXTokenKind kind)
{
    switch (kind)
    {
    case CXToken_Punctuation:
        return "Punctuation";
        break;
    case CXToken_Keyword:
        return "Keyword";
        break;
    case CXToken_Identifier:
        return "Identifier";
        break;
    case CXToken_Literal:
        return "Literal";
        break;
    case CXToken_Comment:
        return "Comment";
        break;
    default:
        return "Unknown";
        break;
    }
}

void showAllTokens(const CXTranslationUnit &tu, const CXToken *tokens,
                   unsigned numTokens)
{
    printf("=== show tokens ===\n");
    printf("NumTokens: %d\n", numTokens);
    for (auto i = 0U; i < numTokens; i++)
    {
        const CXToken &token = tokens[i];
        CXTokenKind kind = clang_getTokenKind(token);
        CXString spell = clang_getTokenSpelling(tu, token);
        CXSourceLocation loc = clang_getTokenLocation(tu, token);

        CXFile file;
        unsigned line, column, offset;
        clang_getFileLocation(loc, &file, &line, &column, &offset);
        CXString fileName = clang_getFileName(file);

        printf("Token: %d\n", i);
        printf(" Text: %s\n", clang_getCString(spell));
        printf(" Kind: %s\n", _getTokenKindSpelling(kind));
        printf(" Location: %s:%d:%d:%d\n", clang_getCString(fileName), line, column,
               offset);
        printf("\n");

        clang_disposeString(fileName);
        clang_disposeString(spell);
    }
}

std::string getCursorKindName(CXCursorKind cursorKind)
{
    CXString kindName = clang_getCursorKindSpelling(cursorKind);
    std::string result = clang_getCString(kindName);

    clang_disposeString(kindName);
    return result;
}

std::string getCursorSpelling(CXCursor cursor)
{
    CXString cursorSpelling = clang_getCursorSpelling(cursor);
    std::string result = clang_getCString(cursorSpelling);

    clang_disposeString(cursorSpelling);
    return result;
}

CXChildVisitResult visitor(CXCursor cursor, CXCursor /* parent */,
                           CXClientData clientData)
{
    CXSourceLocation location = clang_getCursorLocation(cursor);
    if (clang_Location_isFromMainFile(location) == 0)
        return CXChildVisit_Continue;

    CXCursorKind cursorKind = clang_getCursorKind(cursor);

    unsigned int curLevel = *(reinterpret_cast<unsigned int *>(clientData));
    unsigned int nextLevel = curLevel + 1;

    std::cout << std::string(curLevel, '-') << " "
              << getCursorKindName(cursorKind) << " ("
              << getCursorSpelling(cursor) << ")\n";

    clang_visitChildren(cursor, visitor, &nextLevel);

    return CXChildVisit_Continue;
}

unsigned getKeywordCount(const CXTranslationUnit &unit, const char *filename)
{
    unsigned numTokens, numKeywords = 0;
    CXSourceRange range = getFilerange(unit, filename);
    CXToken *tokens;
    clang_tokenize(unit, range, &tokens, &numTokens);
    for (auto i = 0U; i < numTokens; i++)
        if (clang_getTokenKind(tokens[i]) == CXToken_Keyword)
            numKeywords++;
    clang_disposeTokens(unit, tokens, numTokens);
    return numKeywords;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 4)
    {
        cout << "Parse filename [level] [options ...]";
        exit(1);
    }

    const char *filename = argv[1];
    char x;
    int level = 4;
    if (argc > 2)
    {
        int temp = atoi(argv[2]);
        if (1 <= temp && temp <= 3)
            level = temp;
    }

    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit unit = clang_parseTranslationUnit(
        index, filename, nullptr, 0, nullptr, 0, CXTranslationUnit_None);
    if (unit == nullptr)
    {
        cerr << "Unable to parse translation unit. Quitting." << endl;
        exit(-1);
    }

    CXCursor cursor = clang_getTranslationUnitCursor(unit);
    unsigned treeLevel = 0;
    clang_visitChildren(cursor, visitor, &treeLevel);
    if (level >= Requirement::Basic)
    {
        cout << "keywords: " << getKeywordCount(unit, filename) << endl;
    }
    if (level >= Requirement::Advanced)
    {
        unsigned numSwitchs = 0;
        vector<CXCursor> cursors;
        clang_visitChildren(
            cursor,
            [](CXCursor c, CXCursor parent, CXClientData cursors)
            {
                CXSourceLocation location = clang_getCursorLocation(c);
                if (clang_Location_isFromMainFile(location) == 0)
                    return CXChildVisit_Continue;

                if (c.kind == CXCursor_SwitchStmt) {
                    ((vector<CXCursor> *)cursors)->push_back(c);
                }
                return CXChildVisit_Recurse;
            },
            &cursors);
        cout << "switch: " << cursors.size() << endl;
        if (cursors.size())
        {
            vector<unsigned> numCases(cursors.size(), 0);
            for (unsigned i = 0; i < cursors.size(); i++)
            {
                clang_visitChildren(
                    cursors[i],
                    [](CXCursor c, CXCursor parent, CXClientData count)
                    {
                        if (c.kind == CXCursor_CompoundStmt)
                        {
                            clang_visitChildren(
                                c,
                                [](CXCursor c, CXCursor parent, CXClientData count)
                                {
                                    if (c.kind == CXCursor_CaseStmt)
                                        (*(unsigned *)count)++;
                                    return CXChildVisit_Continue;
                                },
                                count);
                            return CXChildVisit_Break;
                        }
                        return CXChildVisit_Continue;
                    },
                    &numCases[i]);
            }
            cout << "case:";
            for (unsigned i : numCases)
            {
                cout << " " << i;
            }
            cout << endl;
        }
    }
    if (level >= Requirement::Uplifting)
    {

    }

    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(index);
}