#include <sstream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <QtCore>
#include <getopt.h>
#include <RTags.h>
#include <Location.h>
#include <AtomicString.h>
#include "Database.h"
#include "Mmap.h"

using namespace RTags;
static inline int readLine(FILE *f, char *buf, int max)
{
    assert(!buf == (max == -1));
    if (max == -1)
        max = INT_MAX;
    for (int i=0; i<max; ++i) {
        const int ch = fgetc(f);
        if (ch == '\n' || ch == EOF)
            return i;
        if (buf)
            *buf++ = *reinterpret_cast<const char*>(&ch);
    }
    return -1;
}

static inline std::string lineForLocation(const std::string &location)
{
    std::string fileName, ret;
    unsigned line = 0, col = 0;
    if (parseLocation(location, fileName, line, col)) {
        FILE *f = fopen(fileName.c_str(), "r");
        if (f) {
            for (unsigned i=0; i<line - 1; ++i)
                readLine(f, 0, -1);
            char line[1024] = { 0 };
            readLine(f, line, 1024);
            ret = line;
            fclose(f);
        }
    }
    return ret;
}

static inline void usage(const char* argv0, FILE *f)
{
    fprintf(f,
            "%s [options]...\n"
            "  --help|-h                     Display this help\n"
            "  --db-file|-d [arg]            Use this database file\n"
            "  --print-detected-db-path|-p   Print out the detected database path\n"
            "  --detect-db|-D                Find .rtags.db based on path\n"
            "                                (default when no -d options are specified)\n"
            "  --db-type|-t [arg]            Type of db (leveldb or filedb)\n"
            "  Modes\n"
            "  --follow-symbol|-f [arg]      Follow this symbol (e.g. /tmp/main.cpp:32:1)\n"
            "  --references|-r [arg]         Print references of symbol at arg\n"
            "  --list-symbols|-l [arg]       Print out symbols names matching arg\n"
            "  --files|-P [arg]              Print out files matching arg\n"
            "  --paths-relative-to-root|-n   Print out files matching arg\n"
            "  --find-symbols|-s [arg]       Print out symbols matching arg\n",
            argv0);
}

int main(int argc, char** argv)
{
    struct option longOptions[] = {
        { "help", 0, 0, 'h' },
        { "follow-symbol", 1, 0, 'f' },
        { "db", 1, 0, 'd' },
        { "print-detected-db-path", 0, 0, 'p' },
        { "find-references", 1, 0, 'r' },
        { "find-symbols", 1, 0, 's' },
        { "find-db", 0, 0, 'F' },
        { "list-symbols", 1, 0, 'l' },
        { "files", 1, 0, 'P' }, // some of these should have optional args
        { "paths-relative-to-root", 0, 0, 'n' },
        { "db-type", 1, 0, 't' },
        { 0, 0, 0, 0 },
    };
    const char *shortOptions = "hf:d:r:l:Dps:P:nt:";

    Mmap::init();

    QList<QByteArray> dbPaths;

    enum Mode {
        None,
        FollowSymbol,
        References,
        FindSymbols,
        ListSymbols,
        Files
        // RecursiveReferences,
    } mode = None;
    enum Flag {
        PathsRelativeToRoot = 0x1
    };
    unsigned flags = 0;
    int idx, longIndex;
    QByteArray arg;
    while ((idx = getopt_long(argc, argv, shortOptions, longOptions, &longIndex)) != -1) {
        switch (idx) {
        case '?':
            usage(argv[0], stderr);
            return 1;
        case 'n':
            flags |= PathsRelativeToRoot;
            break;
        case 'p': {
            const QByteArray db = findRtagsDb();
            if (!db.isEmpty()) {
                printf("%s\n", db.constData());
            } else {
                char buffer[500];
                char *ign = getcwd(buffer, 500);
                (void)ign;
                fprintf(stderr, "No db found for %s\n", buffer);
            }
            return 0; }
        case 't':
            setenv("RTAGS_DB_TYPE", optarg, 1);
            break;
        case 'h':
            usage(argv[0], stdout);
            return 0;
        case 'f':
            if (mode != None) {
                fprintf(stderr, "Mode is already set\n");
                return 1;
            }
            arg = optarg;
            mode = FollowSymbol;
            break;
        case 'P':
            if (mode != None) {
                fprintf(stderr, "Mode is already set\n");
                return 1;
            }
            arg = optarg;
            mode = Files;
            break;
        case 'r':
            arg = optarg;
            if (mode != None) {
                fprintf(stderr, "Mode is already set\n");
                return 1;
            }
            mode = References;
            break;
        case 'D': {
            const QByteArray db = findRtagsDb();
            if (!db.isEmpty()) {
                dbPaths.append(db);
            }
            break; }
        case 'd':
            if (optarg && strlen(optarg))
                dbPaths.append(optarg);
            break;
        case 'l':
            if (mode != None) {
                fprintf(stderr, "Mode is already set\n");
                return 1;
            }
            mode = ListSymbols;
            arg = optarg;
            break;
        case 's':
            if (mode != None) {
                fprintf(stderr, "Mode is already set\n");
                return 1;
            }
            mode = FindSymbols;
            arg = optarg;
            break;
        }
    }
    if (dbPaths.isEmpty()) {
        QByteArray db = findRtagsDb();
        if (db.isEmpty() && !arg.isEmpty())
            db = findRtagsDb(arg);
        if (!db.isEmpty())
            dbPaths.append(db);
    }

    if (dbPaths.isEmpty()) {
        fprintf(stderr, "No databases specified\n");
        return 1;
    }
    if ((flags & PathsRelativeToRoot) && mode != Files) {
        fprintf(stderr, "-n only makes sense with -P\n");
        return 1;
    }
    foreach(const QByteArray &dbPath, dbPaths) {
        Database* db = Database::create(dbPath, Database::ReadOnly);
        if (!db->isOpened()) {
            delete db;
            continue;
        }

        switch (mode) {
        case None:
            usage(argv[0], stderr);
            fprintf(stderr, "No mode selected\n");
            return 1;
        case FollowSymbol: {
            Location loc = db->createLocation(arg);
            // printf("%s => %d:%d:%d\n", arg.constData(), loc.file, loc.line, loc.column);
            if (loc.file) {
                const QByteArray out = db->locationToString(db->followLocation(loc));
                if (!out.isEmpty())
                    printf("%s\n", out.constData());
            } else {
                foreach(const Location &l, db->findSymbol(arg)) {
                    const QByteArray out = db->locationToString(db->followLocation(l));
                    if (!out.isEmpty())
                        printf("%s\n", out.constData());
                }
            }
            break; }
        case References: {
            Location loc = db->createLocation(arg);
            // printf("%s => %d:%d:%d\n", arg.constData(), loc.file, loc.line, loc.column);
            if (loc.file) {
                foreach(const Location &l, db->findReferences(loc)) {
                    const QByteArray out = db->locationToString(l);
                    if (!out.isEmpty())
                        printf("%s\n", out.constData());
                }
            } else {
                QSet<QByteArray> printed;
                foreach(const Location &l, db->findSymbol(arg)) {
                    foreach(const Location &r, db->findReferences(l)) {
                        const QByteArray out = db->locationToString(r);
                        if (!out.isEmpty() && !printed.contains(out)) {
                            printed.insert(out);
                            printf("%s\n", out.constData());
                        }
                    }
                }
            }
            break; }
        case FindSymbols:
            foreach(const Location &loc, db->findSymbol(arg)) {
                const QByteArray out = db->locationToString(loc);
                if (!out.isEmpty())
                    printf("%s\n", out.constData());
            }
            break;
        case ListSymbols:
            foreach(const QByteArray &symbol, db->symbolNames(arg)) {
                printf("%s\n", symbol.constData());
            }
            break;
        case Files: {
            QSet<Path> paths = db->read<QSet<Path> >("files");
            const bool empty = arg.isEmpty();
            const char *root = "./";
            Path srcDir;
            if (!(flags & PathsRelativeToRoot)) {
                srcDir = db->read<Path>("sourceDir");
                root = srcDir.constData();
            }
            foreach(const Path &path, paths) {
                if (empty || path.contains(arg)) {
                    printf("%s%s\n", root, path.constData());
                }
            }
            break; }
        }
    }

    return 0;
}
