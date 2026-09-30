#include <core/composer_store.h>
#include <core/entry_io.h>
#include <core/entry_search.h>
#include <core/entry_store.h>
#include <core/facet_schema.h>
#include <core/pipeline.h>
#include <core/prompt.h>
#include <core/rule_eval.h>
#include <core/rule_io.h>
#include <core/settings.h>
#include <core/run.h>
#include <core/tag_facets.h>
#include <core/tag_groups.h>
#include <core/variables.h>
#include <core/workflow_io.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRandomGenerator>
#include <QTextStream>
#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr qsizetype kShow = 10;

QByteArray slurp(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

QString lineAround(const QByteArray& s, qsizetype pos)
{
    qsizetype begin = 0;
    for (qsizetype i = std::min(pos, s.size()) - 1; i >= 0; --i) {
        if (s[i] == '\n') {
            begin = i + 1;
            break;
        }
    }
    qsizetype end = s.indexOf('\n', begin);
    if (end < 0) end = s.size();
    return QString::fromUtf8(s.mid(begin, end - begin)).trimmed();
}

void reportDiff(QTextStream& out, const QByteArray& a, const QByteArray& b)
{
    out << "    original  " << a.size() << " bytes, " << a.count('\n') << " newlines\n"
        << "    rewritten " << b.size() << " bytes, " << b.count('\n') << " newlines\n";

    const qsizetype n = std::min(a.size(), b.size());
    qsizetype i = 0;
    while (i < n && a[i] == b[i]) ++i;

    out << "    first difference at byte " << i << ", line " << (a.left(i).count('\n') + 1) << "\n"
        << "      original:  " << lineAround(a, i) << "\n"
        << "      rewritten: " << lineAround(b, i) << "\n";
}

bool checkFacets(QTextStream& out, const QDir& dir)
{
    const QString schemaPath = dir.filePath(u"facets.fct"_s);
    const QString defsPath = dir.filePath(u"tag_definitions.fct"_s);

    const std::expected<tc::FacetSchema, tc::LoadError> schema = tc::readFacetSchema(schemaPath);
    if (!schema) {
        out << "facets.fct: " << schema.error().reason << "\n";
        return false;
    }
    const std::expected<tc::TagFacetsFile, tc::LoadError> defs = tc::readTagFacets(defsPath);
    if (!defs) {
        out << "tag_definitions.fct: " << defs.error().reason << "\n";
        return false;
    }

    out << "facets.fct            categories=" << schema->categories().size()
        << " facets=" << schema->facets().size() << "\n"
        << "tag_definitions.fct   tags=" << defs->defs.size()
        << " header=" << defs->header.size() << " warnings=" << defs->warnings.size() << "\n";

    for (qsizetype i = 0; i < defs->warnings.size() && i < kShow; ++i)
        out << "    " << defs->warnings[i].path << "  " << defs->warnings[i].reason << "\n";

    const QString tmp = QDir::temp().filePath(u"tc_tag_definitions.tmp"_s);
    if (const std::expected<void, tc::LoadError> w = tc::writeTagFacets(*defs, tmp); !w) {
        out << "    write failed -- " << w.error().reason << "\n";
        return false;
    }
    const QByteArray before = slurp(defsPath);
    const QByteArray after = slurp(tmp);
    QFile::remove(tmp);

    const bool same = before == after;
    out << "    bytes           " << (same ? "identical" : "DIFFER") << "\n";
    if (!same) reportDiff(out, before, after);

    const QList<tc::FacetIssue> issues = tc::unknownFacets(defs->defs, *schema);
    out << "    unknown facets  " << issues.size() << "\n";
    for (qsizetype i = 0; i < issues.size() && i < kShow; ++i)
        out << "      " << issues[i].tag << " -> " << issues[i].facet << "\n";

    return same;
}

bool checkRules(QTextStream& out, const QDir& dir)
{
    const QString path = dir.filePath(u"rules.fct"_s);

    const std::expected<tc::RuleFile, tc::LoadError> rf = tc::readRules(path);
    if (!rf) {
        out << "rules.fct: " << rf.error().reason << "\n";
        return false;
    }

    qsizetype clauses = 0;
    qsizetype forced = 0;
    qsizetype enabled = 0;
    for (const tc::Rule& r : rf->rules) {
        for (const QList<tc::MatchClause>& g : r.match.orGroups)
            clauses += g.size();
        if (r.force) ++forced;
        if (r.enabled) ++enabled;
    }

    out << "rules.fct             rules=" << rf->rules.size() << " enabled=" << enabled
        << " force=" << forced << " clauses=" << clauses << "\n"
        << "    header=" << rf->header.size() << " errors=" << rf->errors.size()
        << " generatedUuids=" << (rf->generatedUuids ? "yes" : "no") << "\n";

    for (qsizetype i = 0; i < rf->errors.size() && i < kShow; ++i)
        out << "      " << rf->errors[i].reason << "\n";

    const QString tmp = QDir::temp().filePath(u"tc_rules.tmp"_s);
    if (const std::expected<void, tc::LoadError> w = tc::writeRules(*rf, tmp); !w) {
        out << "    write failed -- " << w.error().reason << "\n";
        return false;
    }

    const QByteArray before = slurp(path);
    const QByteArray after = slurp(tmp);
    const bool same = before == after;
    out << "    bytes           " << (same ? "identical" : "DIFFER") << "\n";
    if (!same) reportDiff(out, before, after);

    const std::expected<tc::RuleFile, tc::LoadError> again = tc::readRules(tmp);
    QFile::remove(tmp);

    bool structural = false;
    if (!again) {
        out << "    reparse failed -- " << again.error().reason << "\n";
    }
    else {
        structural = (again->rules == rf->rules);
        out << "    reparse         " << (structural ? "identical" : "DIFFERS") << "\n";
        if (!structural) {
            const qsizetype n = std::min(again->rules.size(), rf->rules.size());
            for (qsizetype i = 0; i < n; ++i)
                if (!(again->rules[i] == rf->rules[i])) {
                    out << "      first mismatch: rule " << i << " \"" << rf->rules[i].name << "\"\n"
                        << "        original:  " << tc::serializeMatch(rf->rules[i].match) << "\n"
                        << "        reparsed:  " << tc::serializeMatch(again->rules[i].match) << "\n";
                    break;
                }
            if (again->rules.size() != rf->rules.size())
                out << "      count " << rf->rules.size() << " -> " << again->rules.size() << "\n";
        }
    }

    return same && structural;
}

QString resultName(tc::TagResult r)
{
    switch (r) {
    case tc::TagResult::Include: return u"Include"_s;
    case tc::TagResult::Skipped: return u"Skipped"_s;
    case tc::TagResult::Replaced: return u"Replaced"_s;
    case tc::TagResult::Injected: return u"Injected"_s;
    case tc::TagResult::Flagged: return u"Flagged"_s;
    case tc::TagResult::NoFacets: return u"NoFacets"_s;
    case tc::TagResult::Deactivated: return u"Deactivated"_s;
    case tc::TagResult::Deleted: return u"Deleted"_s;
    }
    return u"?"_s;
}

const tc::PipelineTag* findTag(const QList<tc::PipelineTag>& tags, const QString& name)
{
    for (const tc::PipelineTag& pt : tags)
        if (pt.tag == name) return &pt;
    return nullptr;
}

struct Expect {
    QString tag;
    tc::TagResult result;
    float weight;
};

qsizetype checkExpectations(QTextStream& out, const QString& label,
                            const QList<tc::PipelineTag>& got, const QList<Expect>& want)
{
    qsizetype failed = 0;

    if (got.size() != want.size()) {
        out << "    " << label << " FAIL  size " << got.size() << " want " << want.size() << "\n";
        for (const tc::PipelineTag& pt : got)
            out << "      got: " << pt.tag << " " << resultName(pt.result) << "\n";
        ++failed;
    }

    for (const Expect& e : want) {
        const tc::PipelineTag* pt = findTag(got, e.tag);
        if (!pt) {
            out << "    " << label << " FAIL  missing \"" << e.tag << "\"\n";
            ++failed;
            continue;
        }
        if (pt->result != e.result) {
            out << "    " << label << " FAIL  \"" << e.tag << "\" is " << resultName(pt->result)
                << " want " << resultName(e.result) << "\n";
            ++failed;
        }
        if (qAbs(pt->weight - e.weight) > 0.001f) {
            out << "    " << label << " FAIL  \"" << e.tag << "\" weight " << pt->weight << " want "
                << e.weight << "\n";
            ++failed;
        }
    }
    return failed;
}

bool checkPipeline(QTextStream& out)
{
    tc::TagFacets defs;
    defs.set(u"blue eyes"_s, {u"Eyes"_s, u"Color"_s});
    defs.set(u"1girl"_s, {u"Count"_s});
    defs.set(u"hat"_s, {u"Headwear"_s});

    tc::Variables vars;
    vars.set(u"color"_s, u"blue"_s);

    tc::Rule noHats;
    noHats.name = u"NoHats"_s;
    noHats.match = tc::parseMatch(u"anyTag(facets: Headwear)"_s);
    noHats.action = *tc::parseAction(u"delete"_s);
    const QList<tc::Rule> rules = {noHats};

    const tc::PipelineContext ctx{&defs, &rules, &vars};

    const QList<Expect> want = {
        {u"1girl"_s, tc::TagResult::Include, 1.0f},
        {u"hat"_s, tc::TagResult::Deleted, 1.0f},
        {u"blue eyes"_s, tc::TagResult::Include, 0.8f},
        {u"undefined tag"_s, tc::TagResult::NoFacets, 1.0f},
        {u"muted"_s, tc::TagResult::Deactivated, 1.0f},
        // Deactivated rows keep their raw text (no sourceTag).
        {u"$color$ muted"_s, tc::TagResult::Deactivated, 1.0f},
    };

    tc::ComposerDoc a;
    a.activeTags = {u"1girl"_s,         u"hat"_s,           u"blue eyes"_s,
                    u"$color$ eyes"_s,  u"undefined tag"_s, u"muted"_s,
                    u"$color$ muted"_s};
    a.deactivated = {u"muted"_s, u"$color$ muted"_s};
    a.weights.insert(u"blue eyes"_s, 0.8f);

    tc::ComposerDoc b = a;
    b.activeTags = {u"1girl"_s,     u"hat"_s,           u"$color$ eyes"_s,
                    u"blue eyes"_s, u"undefined tag"_s, u"muted"_s,
                    u"$color$ muted"_s};

    const QList<tc::PipelineTag> ra = tc::evaluate(a, ctx);
    const QList<tc::PipelineTag> rb = tc::evaluate(b, ctx);

    out << "pipeline              in=" << a.activeTags.size() << " out=" << ra.size()
        << " (literal first), out=" << rb.size() << " (variable first)\n";

    qsizetype failed = checkExpectations(out, u"literal-first"_s, ra, want);
    failed += checkExpectations(out, u"variable-first"_s, rb, want);

    for (const auto& [label, result] : {std::pair{u"literal-first"_s, &ra},
                                        std::pair{u"variable-first"_s, &rb}}) {
        const tc::PipelineTag* muted = findTag(*result, u"$color$ muted"_s);
        if (muted && !muted->sourceTag.isEmpty()) {
            out << "    " << label << " deactivated-variable FAIL  sourceTag \""
                << muted->sourceTag << "\" want empty" << Qt::endl;
            ++failed;
        }
    }

    const tc::PipelineTag* collapsed = findTag(rb, u"blue eyes"_s);
    if (collapsed && collapsed->sourceTag != u"$color$ eyes"_s) {
        out << "    survivor sourceTag \"" << collapsed->sourceTag << "\" want \"$color$ eyes\"\n";
        ++failed;
    }

    out << "    cases           " << (failed == 0 ? "all ok" : "FAILURES") << "\n";
    return failed == 0;
}

bool entryHasTerm(const tc::Entry& e, const tc::TagTerm& term)
{
    for (const tc::EntryImage& image : e.images)
        for (const QString& tag : image.tags)
            if (term.exact ? (tag == term.text) : tag.startsWith(term.text)) return true;
    return false;
}

// Brute-force reference for the tag clauses, to check the inverted index.
QStringList bruteForceTagSearch(const QList<tc::Entry>& entries, const tc::EntryQuery& query)
{
    QList<const tc::Entry*> matched;
    QSet<qsizetype> seen;

    for (const tc::QueryGroup& group : query.groups) {
        for (qsizetype i = 0; i < entries.size(); ++i) {
            if (seen.contains(i)) continue;

            bool ok = true;
            for (const tc::TagTerm& term : group.tags)
                if (!entryHasTerm(entries[i], term)) {
                    ok = false;
                    break;
                }
            if (ok)
                for (const tc::TagTerm& term : group.excluded)
                    if (entryHasTerm(entries[i], term)) {
                        ok = false;
                        break;
                    }
            if (!ok) continue;

            seen.insert(i);
            matched << &entries[i];
        }
    }

    tc::sortEntries(matched, query.sortKey, query.sortDir);

    QStringList uuids;
    for (const tc::Entry* e : matched)
        uuids << e->uuid;
    return uuids;
}

bool checkSearch(QTextStream& out)
{
    const QString root = QDir::temp().filePath(u"tc_search_test"_s);
    QDir(root).removeRecursively();
    QDir().mkpath(root);

    qsizetype failed = 0;
    const auto expect = [&out, &failed](const QString& label, const QString& got,
                                        const QString& want) {
        if (got == want) return;
        ++failed;
        out << "    " << label << " FAIL\n      got  " << got << "\n      want " << want << "\n";
    };

    tc::EntryStore store;
    store.load(root);

    const auto makeEntry = [&store](const QString& title, const QStringList& tags,
                                    qsizetype images, bool lora, const QString& comment,
                                    qint64 created) {
        tc::Entry e;
        e.title = title;
        e.comment = comment;
        e.created = created;
        for (qsizetype i = 0; i < images; ++i)
            e.images << tc::EntryImage{QString::number(i) + u".png"_s, i == 0 ? tags : QStringList{}};
        if (lora) e.lora = tc::Lora{u"primary"_s, u"chars/saber_v2.safetensors"_s, u"abc123"_s};
        return store.add(std::move(e));
    };

    // Distinct creation times so the default sort is deterministic.
    const QString a =
        makeEntry(u"Alpha"_s, {u"blue eyes"_s, u"blonde hair"_s, u"hat"_s}, 1, true, u"note"_s, 100);
    const QString b =
        makeEntry(u"Beta"_s, {u"blue eyes"_s, u"black hair"_s}, 3, false, QString(), 200);
    const QString c =
        makeEntry(u"Gamma"_s, {u"blue"_s, u"red eyes"_s}, 2, false, u"other"_s, 300);

    tc::EntrySearch search(store);

    const auto ids = [&](const QString& q) {
        QStringList names;
        for (const QString& uuid : search.find(q))
            if (const tc::Entry* e = store.find(uuid)) names << e->title;
        return names.join(u","_s);
    };

    expect(u"empty query"_s, ids(QString()), u"Gamma,Beta,Alpha"_s);
    expect(u"prefix"_s, ids(u"blue"_s), u"Gamma,Beta,Alpha"_s);
    expect(u"exact"_s, ids(u"blue]"_s), u"Gamma"_s);
    expect(u"and"_s, ids(u"blue eyes, blonde"_s), u"Alpha"_s);
    expect(u"negation"_s, ids(u"blue eyes, -hat"_s), u"Beta"_s);
    expect(u"or groups"_s, ids(u"blonde | red eyes"_s), u"Gamma,Alpha"_s);
    expect(u"title"_s, ids(u"title:bet"_s), u"Beta"_s);
    expect(u"comment"_s, ids(u"comment:oth"_s), u"Gamma"_s);
    expect(u"lora name"_s, ids(u"lora:saber"_s), u"Alpha"_s);
    expect(u"lora hash"_s, ids(u"lora:abc"_s), u"Alpha"_s);
    expect(u"has lora"_s, ids(u"has:lora"_s), u"Alpha"_s);
    expect(u"missing lora"_s, ids(u"missing:lora"_s), u"Gamma,Beta"_s);
    expect(u"missing comment"_s, ids(u"missing:comment"_s), u"Beta"_s);
    expect(u"images gt"_s, ids(u"images:>1"_s), u"Gamma,Beta"_s);
    expect(u"images eq"_s, ids(u"images:3"_s), u"Beta"_s);
    expect(u"tags le"_s, ids(u"tags:<=2"_s), u"Gamma,Beta"_s);
    expect(u"sort title asc"_s, ids(u"sort:title"_s), u"Alpha,Beta,Gamma"_s);
    expect(u"sort title desc"_s, ids(u"sort:title:desc"_s), u"Gamma,Beta,Alpha"_s);
    expect(u"sort images"_s, ids(u"sort:images"_s), u"Beta,Gamma,Alpha"_s);
    expect(u"unknown has field ignored"_s, ids(u"has:banana"_s), u"Gamma,Beta,Alpha"_s);
    expect(u"no match"_s, ids(u"nonexistent"_s), QString());

    // An edit must invalidate the index rather than serve a stale answer.
    store.addTag(b, 0, u"hat"_s);
    expect(u"reindex after edit"_s, ids(u"blue eyes, -hat"_s), QString());
    store.remove(c);
    expect(u"reindex after remove"_s, ids(u"blue"_s), u"Beta,Alpha"_s);

    QDir(root).removeRecursively();

    out << "search                clauses=" << (failed == 0 ? "all ok" : "FAILURES") << "\n";
    return failed == 0;
}

void showSearchTiming(QTextStream& out, const QString& entryDir)
{
    tc::EntryStore store;
    store.load(entryDir);
    if (store.count() == 0) {
        out << "search timing: no entries in " << entryDir << "\n";
        return;
    }

    tc::EntrySearch search(store);

    const QStringList queries = {u"blonde"_s, u"blue eyes"_s, u"blonde hair, blue eyes"_s,
                                 u"hair"_s, u"1girl, -hat"_s};

    out << "search timing         entries=" << store.count()
        << " indexedTags=" << search.indexedTags() << "\n";

    qsizetype mismatches = 0;
    for (const QString& q : queries) {
        const tc::EntryQuery parsed = tc::parseEntryQuery(q);

        QElapsedTimer timer;
        timer.start();
        QStringList viaIndex;
        for (int i = 0; i < 20; ++i)
            viaIndex = search.find(parsed);
        const double indexUs = double(timer.nsecsElapsed()) / 20000.0;

        timer.restart();
        QStringList viaScan;
        for (int i = 0; i < 20; ++i)
            viaScan = bruteForceTagSearch(store.all(), parsed);
        const double scanUs = double(timer.nsecsElapsed()) / 20000.0;

        const bool agree = viaIndex == viaScan;
        if (!agree) ++mismatches;

        out << "    " << q.leftJustified(26) << viaIndex.size() << " hits   index "
            << QString::number(indexUs, 'f', 1).rightJustified(8) << " us   scan "
            << QString::number(scanUs, 'f', 1).rightJustified(8) << " us   "
            << (agree ? "agree" : "MISMATCH") << "\n";
    }

    if (mismatches > 0) out << "    " << mismatches << " query/queries disagree with the scan\n";
}

bool checkEntryStore(QTextStream& out)
{
    const QString root = QDir::temp().filePath(u"tc_entry_store_test"_s);
    QDir(root).removeRecursively();
    QDir().mkpath(root);

    qsizetype failed = 0;
    const auto expect = [&out, &failed](const QString& label, const QString& got,
                                        const QString& want) {
        if (got == want) return;
        ++failed;
        out << "    " << label << " FAIL\n      got  " << got << "\n      want " << want << "\n";
    };

    tc::EntryStore store;
    store.load(root);

    qsizetype added = 0, changed = 0, removed = 0, imagesRemoved = 0;
    QObject::connect(&store, &tc::EntryStore::entryAdded, [&added]() { ++added; });
    QObject::connect(&store, &tc::EntryStore::entryChanged, [&changed]() { ++changed; });
    QObject::connect(&store, &tc::EntryStore::entryRemoved, [&removed]() { ++removed; });
    QObject::connect(&store, &tc::EntryStore::imageRemoved, [&imagesRemoved]() { ++imagesRemoved; });

    tc::Entry e;
    e.title = u"Test Entry"_s;
    e.images = {tc::EntryImage{u"1.png"_s, {u"1girl"_s}}, tc::EntryImage{u"2.png"_s, {u"solo"_s}}};
    const QString uuid = store.add(std::move(e));

    expect(u"add returns uuid"_s, uuid.isEmpty() ? u"empty"_s : u"ok"_s, u"ok"_s);
    expect(u"count"_s, QString::number(store.count()), u"1"_s);

    store.addTag(uuid, 0, u"blue eyes"_s);
    store.addTag(uuid, 0, u"blue eyes"_s); // duplicate, must be rejected
    store.setTitle(uuid, u"Renamed"_s);
    store.setLora(uuid, tc::Lora{u"primary"_s, u"a/b.safetensors"_s, {}, 0.7, 0.8});

    // Written through, so a second store reading the same folder sees it all.
    tc::EntryStore reread;
    reread.load(root);
    const tc::Entry* got = reread.find(uuid);

    expect(u"persisted"_s, got ? u"found"_s : u"missing"_s, u"found"_s);
    if (got) {
        expect(u"title"_s, got->title, u"Renamed"_s);
        expect(u"tags"_s, got->images[0].tags.join(u","_s), u"1girl,blue eyes"_s);
        expect(u"lora"_s, got->lora ? got->lora->file : u"none"_s, u"a/b.safetensors"_s);
        expect(u"lora strengths"_s,
               got->lora ? QString::number(got->lora->modelStrength, 'f', 1) + u"/"_s
                       + QString::number(got->lora->clipStrength, 'f', 1)
                         : u"none"_s,
               u"0.7/0.8"_s);
    }

    store.removeImage(uuid, 0);
    const tc::Entry* afterImage = store.find(uuid);
    expect(u"image removed"_s, afterImage ? afterImage->images[0].fileName : u"gone"_s, u"2.png"_s);

    // Dropping the last image takes the whole entry with it.
    store.removeImage(uuid, 0);
    expect(u"entry gone with last image"_s, QString::number(store.count()), u"0"_s);
    expect(u"folder gone"_s, QDir(root).exists(uuid) ? u"exists"_s : u"gone"_s, u"gone"_s);
    expect(u"find after remove"_s, store.find(uuid) ? u"found"_s : u"null"_s, u"null"_s);

    expect(u"signals"_s,
           QString::number(added) + u"/"_s + QString::number(changed) + u"/"_s
               + QString::number(removed) + u"/"_s + QString::number(imagesRemoved),
           u"1/4/1/1"_s);

    QDir(root).removeRecursively();

    out << "entry store           temp=" << root << "\n"
        << "    cases           " << (failed == 0 ? "all ok" : "FAILURES") << "\n";
    return failed == 0;
}

bool checkRun(QTextStream& out)
{
    tc::TagFacets defs;
    defs.set(u"1girl"_s, {u"Count"_s});
    defs.set(u"blue eyes"_s, {u"Eyes"_s});
    defs.set(u"forest"_s, {u"Scene"_s});
    defs.set(u"desert"_s, {u"Scene"_s});

    tc::TagGroups groups;
    groups.setAll({tc::TagGroup{u"Subject"_s, {u"Count"_s}}, tc::TagGroup{u"Eyes"_s, {u"Eyes"_s}},
                   tc::TagGroup{u"Scene"_s, {u"Scene"_s}}});

    tc::Workflow wf;
    wf.vars = {
        tc::WorkflowVar{u"__seed__"_s, tc::SeedVar{tc::SeedBehavior::Increment, 41}},
        tc::WorkflowVar{u"__negative__"_s, tc::StringVar{u"blurry, \"bad\" art"_s}},
        tc::WorkflowVar{u"__steps__"_s, tc::IntVar{28}},
        tc::WorkflowVar{u"__cfg__"_s, tc::FloatVar{4.5}},
        tc::WorkflowVar{u""_s, tc::LatentSizeVar{960, 1088, u"__latentw__"_s, u"__latenth__"_s}},
        tc::WorkflowVar{u"__image__"_s, tc::ImageVar{u"abc-123"_s}},
        tc::WorkflowVar{u"__wild__"_s, tc::WildcardVar{{u"forest"_s, u"desert"_s}}},
    };

    const QString tmpl =
        uR"({"seed":__seed__,"neg":__negative__,"steps":__steps__,"cfg":__cfg__,)"_s
        + uR"("w":__latentw__,"h":__latenth__,"img":__image__,"pos":"__positive__",)"_s
        + uR"("lora":__lora_name_1__,"ls":__lora_model_str_1__})"_s;

    tc::ComposerDoc doc;
    doc.activeTags = {u"1girl"_s, u"blue eyes"_s};

    tc::RenderContext ctx;
    ctx.pipeline = tc::PipelineContext{&defs, nullptr, nullptr};
    ctx.groups = &groups;
    ctx.imageSubfolder = u"tagcomposer"_s;
    ctx.loraSlots = 1;

    QRandomGenerator rng(1234u);
    const tc::RunRequest a = tc::renderRun(doc, wf, tmpl, ctx, &rng);

    QRandomGenerator rng2(1234u);
    const tc::RunRequest b = tc::renderRun(doc, wf, tmpl, ctx, &rng2);

    qsizetype failed = 0;
    const auto expect = [&out, &failed](const QString& label, const QString& got,
                                        const QString& want) {
        if (got == want) return;
        ++failed;
        out << "    " << label << " FAIL\n      got  " << got << "\n      want " << want << "\n";
    };

    expect(u"determinism"_s, a.json, b.json);
    expect(u"prompt"_s, a.positivePrompt,
           u"1girl, blue eyes, "_s + a.wildcardTags.value(0));
    expect(u"seed advance"_s,
           QString::number(a.nextSeeds.size()) + u":"_s
               + (a.nextSeeds.isEmpty() ? QString() : QString::number(a.nextSeeds[0].value)),
           u"1:42"_s);

    const QString wantJson =
        uR"({"seed":42,"neg":"blurry, \"bad\" art","steps":28,"cfg":4.500000,)"_s
        + uR"("w":960,"h":1088,"img":"tagcomposer/abc-123.png","pos":"1girl, blue eyes, )"_s
        + a.wildcardTags.value(0) + uR"(","lora":"None","ls":0.900000})"_s;
    expect(u"json"_s, a.json, wantJson);

    // Backslashes, quotes and control characters still render valid JSON.
    tc::Workflow escWf;
    escWf.vars = {tc::WorkflowVar{u"__note__"_s, tc::StringVar{u"tab\there\nnext"_s}}};
    tc::ComposerDoc escDoc;
    escDoc.activeTags = {u"\\m/"_s, u"say \"hi\""_s};
    const tc::RunRequest esc =
        tc::renderRun(escDoc, escWf, uR"({"pos":__positive__,"note":__note__})"_s, ctx, nullptr);
    const QJsonObject parsed = QJsonDocument::fromJson(esc.json.toUtf8()).object();
    expect(u"escaped prompt"_s, esc.positivePrompt, u"\\m/, say \"hi\""_s);
    expect(u"escaped pos"_s, parsed.value(u"pos"_s).toString(), esc.positivePrompt);
    expect(u"escaped note"_s, parsed.value(u"note"_s).toString(), u"tab\there\nnext"_s);

    if (a.wildcardTags.size() != 1
        || (a.wildcardTags[0] != u"forest"_s && a.wildcardTags[0] != u"desert"_s)) {
        ++failed;
        out << "    wildcard FAIL  " << a.wildcardTags.join(u","_s) << "\n";
    }

    tc::ComposerDoc withLora = doc;
    withLora.loraStack = {tc::Lora{u"primary"_s, u"style/x.safetensors"_s, {}, 1.0, 1.0},
                          tc::Lora{u"primary"_s, u"style/y.safetensors"_s, {}, 1.0, 1.0}};

    const tc::RunIssues clean = tc::validateRun(doc, wf, tmpl, 1);
    const tc::RunIssues bad = tc::validateRun(withLora, wf, tmpl + u"  __mystery__"_s, 2);

    expect(u"clean errors"_s, clean.errors.join(u" | "_s), QString());
    expect(u"clean warnings"_s, clean.warnings.join(u" | "_s), QString());
    expect(u"bad errors"_s, bad.errors.join(u" | "_s),
           u"unresolved token(s): __mystery__ | "_s
               + u"2 LoRA(s) active but missing slot(s): __lora_name_2__"_s);

    const tc::Workflow unusedVar{{}, {}, {}, 0, {tc::WorkflowVar{u"__gone__"_s, tc::IntVar{1}}}};
    const tc::RunIssues warned = tc::validateRun(doc, unusedVar, u"{}"_s, 1);
    expect(u"unused warns only"_s,
           QString::number(warned.errors.size()) + u"/"_s + warned.warnings.join(u""_s),
           u"0/unused variable(s): __gone__"_s);

    out << "run                   wildcard=" << a.wildcardTags.value(0)
        << " seeds=" << a.nextSeeds.size() << " blocked=" << (bad.blocked() ? "yes" : "no") << "\n"
        << "    cases           " << (failed == 0 ? "all ok" : "FAILURES") << "\n";
    return failed == 0;
}

bool checkSettings(QTextStream& out, const QDir& dir)
{
    const QString path = dir.filePath(u"settings.json"_s);

    const std::expected<tc::Settings, tc::LoadError> loaded = tc::readSettings(path);
    if (!loaded) {
        out << "settings.json: " << loaded.error().reason << "\n";
        return false;
    }

    const QString tmp = QDir::temp().filePath(u"tc_settings.tmp"_s);
    if (const std::expected<void, tc::LoadError> w = tc::writeSettings(*loaded, tmp); !w) {
        out << "    write failed -- " << w.error().reason << "\n";
        return false;
    }

    const QByteArray before = slurp(path);
    const QByteArray after = slurp(tmp);
    const bool same = before == after;

    const std::expected<tc::Settings, tc::LoadError> again = tc::readSettings(tmp);
    QFile::remove(tmp);
    const bool structural = again && (*again == *loaded);

    out << "settings.json         comfy=" << (loaded->comfyEnabled ? "on" : "off")
        << " host=" << loaded->comfyServerAddress << " formats=" << loaded->facetFormats.size()
        << "\n"
        << "    bytes           " << (same ? "identical" : "DIFFER") << "\n"
        << "    reparse         " << (structural ? "identical" : "DIFFERS") << "\n";
    if (!same) reportDiff(out, before, after);

    // A missing file reads as defaults.
    const std::expected<tc::Settings, tc::LoadError> absent =
        tc::readSettings(QDir::temp().filePath(u"tc_no_such_settings.json"_s));
    const bool defaulted = absent && *absent == tc::Settings{};
    out << "    missing file    " << (defaulted ? "defaults" : "FAILED") << "\n";

    return structural && defaulted;
}

bool checkWorkflows(QTextStream& out, const QDir& dir)
{
    const QString path = dir.filePath(u"workflows.json"_s);

    const std::expected<tc::WorkflowsFile, tc::LoadError> wf = tc::readWorkflows(path);
    if (!wf) {
        out << "workflows.json: " << wf.error().reason << "\n";
        return false;
    }

    QMap<QString, qsizetype> byType;
    qsizetype vars = 0;
    for (const tc::Workflow& w : wf->workflows) {
        vars += w.vars.size();
        for (const tc::WorkflowVar& v : w.vars)
            ++byType[tc::varTypeName(v.value)];
    }

    out << "workflows.json        files=" << wf->workflows.size() << " vars=" << vars
        << " selected=" << wf->selectedIndex << " warnings=" << wf->warnings.size() << "\n";
    for (auto it = byType.cbegin(); it != byType.cend(); ++it)
        out << "    " << it.key().leftJustified(12) << it.value() << "\n";
    for (qsizetype i = 0; i < wf->warnings.size() && i < kShow; ++i)
        out << "    " << wf->warnings[i].reason << "\n";

    const QString tmp = QDir::temp().filePath(u"tc_workflows.tmp"_s);
    if (const std::expected<void, tc::LoadError> w = tc::writeWorkflows(*wf, tmp); !w) {
        out << "    write failed -- " << w.error().reason << "\n";
        return false;
    }

    const QByteArray before = slurp(path);
    const QByteArray after = slurp(tmp);
    const bool same = before == after;
    out << "    bytes           " << (same ? "identical" : "DIFFER") << "\n";
    if (!same) reportDiff(out, before, after);

    const std::expected<tc::WorkflowsFile, tc::LoadError> again = tc::readWorkflows(tmp);
    QFile::remove(tmp);

    bool structural = false;
    if (!again) {
        out << "    reparse failed -- " << again.error().reason << "\n";
    }
    else {
        structural = (again->workflows == wf->workflows
                      && again->selectedIndex == wf->selectedIndex);
        out << "    reparse         " << (structural ? "identical" : "DIFFERS") << "\n";
        if (!structural) {
            const qsizetype n = std::min(again->workflows.size(), wf->workflows.size());
            for (qsizetype i = 0; i < n; ++i)
                if (!(again->workflows[i] == wf->workflows[i])) {
                    out << "      first mismatch: \"" << wf->workflows[i].name << "\"\n";
                    break;
                }
        }
    }

    return structural;
}

QString docSummary(const tc::ComposerDoc& d)
{
    QStringList off(d.deactivated.cbegin(), d.deactivated.cend());
    std::sort(off.begin(), off.end());

    QStringList keys = d.weights.keys();
    std::sort(keys.begin(), keys.end());
    QStringList weights;
    for (const QString& k : keys)
        weights << k + u"="_s + QString::number(double(d.weights.value(k)), 'f', 2);

    QStringList pushes;
    for (const tc::EntryPush& p : d.pushes)
        pushes << p.entryUuid + u"/"_s + p.imageFile;

    return u"tags["_s + d.activeTags.join(u","_s) + u"] off["_s + off.join(u","_s) + u"] w["_s
        + weights.join(u","_s) + u"] push["_s + pushes.join(u","_s) + u"]";
}

bool checkStore(QTextStream& out)
{
    qsizetype failed = 0;
    tc::ComposerStore store;

    const auto step = [&out, &failed, &store](const QString& label, const QString& want) {
        const QString got = docSummary(store.doc());
        if (got == want) return;
        ++failed;
        out << "    " << label << " FAIL\n      got  " << got << "\n      want " << want << "\n";
    };

    qsizetype changes = 0;
    QObject::connect(&store, &tc::ComposerStore::docChanged, [&changes]() { ++changes; });

    store.addTags({u"a"_s, u"b"_s});
    step(u"add"_s, u"tags[a,b] off[] w[] push[]"_s);

    const qsizetype beforeNoop = changes;
    store.addTags({u"a"_s});
    if (changes != beforeNoop) {
        ++failed;
        out << "    no-op add FAIL  emitted docChanged\n";
    }

    store.setDeactivated(u"a"_s, true);
    step(u"deactivate"_s, u"tags[a,b] off[a] w[] push[]"_s);

    store.addTags({u"a"_s});
    step(u"re-add reactivates"_s, u"tags[a,b] off[] w[] push[]"_s);

    store.setWeight(u"b"_s, 1.5f);
    store.setWeight(u"b"_s, 1.6f);
    step(u"weight"_s, u"tags[a,b] off[] w[b=1.60] push[]"_s);

    store.undo();
    step(u"undo coalesced weights"_s, u"tags[a,b] off[] w[] push[]"_s);

    store.redo();
    step(u"redo"_s, u"tags[a,b] off[] w[b=1.60] push[]"_s);

    store.push(tc::EntryPush{u"e1"_s, u"1.png"_s, {u"x"_s, u"y"_s}});
    step(u"push e1"_s, u"tags[a,b,x,y] off[] w[b=1.60] push[e1/1.png]"_s);

    store.push(tc::EntryPush{u"e2"_s, u"1.png"_s, {u"y"_s, u"z"_s}});
    step(u"push e2"_s, u"tags[a,b,x,y,z] off[] w[b=1.60] push[e1/1.png,e2/1.png]"_s);

    store.unpush(u"e1"_s, u"1.png"_s);
    step(u"unpush e1 keeps shared y"_s, u"tags[a,b,y,z] off[] w[b=1.60] push[e2/1.png]"_s);

    store.clear();
    step(u"clear"_s, u"tags[] off[] w[] push[]"_s);

    store.undo();
    step(u"undo clear"_s, u"tags[a,b,y,z] off[] w[b=1.60] push[e2/1.png]"_s);

    // A delete-rule drop takes no undo step, so undo reverts the add before it.
    store.addTags({u"d"_s});
    store.dropTags({u"d"_s});
    step(u"drop"_s, u"tags[a,b,y,z] off[] w[b=1.60] push[e2/1.png]"_s);

    store.undo();
    step(u"undo skips drop"_s, u"tags[a,b,y,z] off[] w[b=1.60] push[e2/1.png]"_s);

    const qsizetype beforeNoopDrop = changes;
    if (store.dropTags({u"not-active"_s}) || changes != beforeNoopDrop) {
        ++failed;
        out << "    no-op drop FAIL  reported a change\n";
    }

    // A LoRA arrives with an entry's first push, leaves with its last, and undoes
    // with the push.
    tc::ComposerStore lora;
    const tc::Lora l1{u"primary"_s, u"a.safetensors"_s, u"sha-a"_s, 1.0, 1.0};
    auto loraCase = [&](const QString& name, qsizetype want) {
        if (lora.doc().loraStack.size() == want) return;
        ++failed;
        out << "    " << name << " FAIL  stack=" << lora.doc().loraStack.size()
            << " want=" << want << " ";
    };

    lora.push(tc::EntryPush{u"e1"_s, u"1.png"_s, {u"x"_s}}, l1);
    lora.push(tc::EntryPush{u"e1"_s, u"2.png"_s, {u"y"_s}}, l1);
    loraCase(u"lora push"_s, 1);

    lora.unpush(u"e1"_s, u"1.png"_s, u"sha-a"_s);
    loraCase(u"lora unpush one of two"_s, 1);

    lora.unpush(u"e1"_s, u"2.png"_s, u"sha-a"_s);
    loraCase(u"lora unpush last"_s, 0);

    lora.undo();
    loraCase(u"lora undo"_s, 1);

    tc::ComposerStore fresh;
    if (fresh.canUndo() || fresh.canRedo()) {
        ++failed;
        out << "    fresh store FAIL  reports undo or redo available\n";
    }
    fresh.addTags({u"q"_s});
    fresh.reset(tc::ComposerDoc{});
    if (fresh.canUndo()) {
        ++failed;
        out << "    reset FAIL  undo stack survived\n";
    }

    out << "store                 signals=" << changes << "\n"
        << "    cases           " << (failed == 0 ? "all ok" : "FAILURES") << "\n";
    return failed == 0;
}

bool checkPrompt(QTextStream& out)
{
    tc::TagFacets defs;
    defs.set(u"blue eyes"_s, {u"Eyes"_s, u"Color"_s});
    defs.set(u"1girl"_s, {u"Count"_s});

    tc::TagGroups groups;
    groups.setAll({tc::TagGroup{u"Subject"_s, {u"Count"_s}}, tc::TagGroup{u"Eyes"_s, {u"Eyes"_s}}});

    const QList<tc::FacetFormat> formats = {{u"Count"_s, u"<"_s, u">"_s}};

    tc::ComposerDoc doc;
    doc.activeTags = {u"1girl"_s, u"blue eyes"_s, u"smile (happy)"_s};
    doc.weights.insert(u"blue eyes"_s, 1.5f);

    const tc::PipelineContext ctx{&defs, nullptr, nullptr};
    const QList<tc::TagBucket> buckets = tc::bucketByGroup(tc::evaluate(doc, ctx), groups);

    QStringList names;
    for (const tc::TagBucket& b : buckets)
        names << (b.group.isEmpty() ? u"<uncategorized>"_s : b.group);

    const QString plain = tc::buildPromptString(buckets, formats);

    const QString wantPlain = u"<1girl>, (blue eyes:1.5), smile \\(happy\\)"_s;
    const QString wantBuckets = u"Subject, Eyes, <uncategorized>"_s;

    qsizetype failed = 0;
    const auto expect = [&out, &failed](const QString& label, const QString& got,
                                        const QString& want) {
        if (got == want) return;
        ++failed;
        out << "    " << label << " FAIL\n      got  " << got << "\n      want " << want << "\n";
    };

    expect(u"buckets"_s, names.join(u", "_s), wantBuckets);
    expect(u"plain"_s, plain, wantPlain);

    out << "prompt                buckets=" << buckets.size() << "\n"
        << "    " << plain << "\n"
        << "    cases           " << (failed == 0 ? "all ok" : "FAILURES") << "\n";

    return failed == 0;
}

void showRealPrompt(QTextStream& out, const QDir& dir, const QString& entryDir)
{
    const std::expected<tc::TagFacetsFile, tc::LoadError> defs =
        tc::readTagFacets(dir.filePath(u"tag_definitions.fct"_s));
    const std::expected<tc::RuleFile, tc::LoadError> rf =
        tc::readRules(dir.filePath(u"rules.fct"_s));
    const std::expected<tc::VariablesFile, tc::LoadError> vf =
        tc::readVariables(dir.filePath(u"vars.fct"_s));
    const std::expected<tc::TagGroupsFile, tc::LoadError> gf =
        tc::readTagGroups(dir.filePath(u"groups.fct"_s));
    if (!defs || !rf || !vf || !gf) {
        out << "real prompt: prerequisites failed\n";
        return;
    }

    const tc::EntryLoad lib = tc::readEntries(entryDir);
    const tc::Entry* pick = nullptr;
    for (const tc::Entry& e : lib.entries)
        if (!e.images.isEmpty() && e.images[0].tags.size() >= 10) {
            pick = &e;
            break;
        }
    if (!pick) {
        out << "real prompt: no entry with 10+ tags in " << entryDir << "\n";
        return;
    }

    tc::ComposerDoc doc;
    doc.activeTags = pick->images[0].tags;

    const tc::PipelineContext ctx{&defs->defs, &rf->rules, &vf->vars};
    const QList<tc::PipelineTag> tags = tc::evaluate(doc, ctx);
    const QList<tc::TagBucket> buckets = tc::bucketByGroup(tags, gf->groups);

    out << "real prompt           entries=" << lib.entries.size() << " using \"" << pick->title
        << "\"\n"
        << "    in=" << doc.activeTags.size() << " resolved=" << tags.size()
        << " buckets=" << buckets.size() << "\n";
    for (const tc::TagBucket& b : buckets)
        out << "      " << (b.group.isEmpty() ? u"<uncategorized>"_s : b.group).leftJustified(24)
            << b.tags.size() << "\n";
    out << "\n    " << tc::buildPromptString(buckets) << "\n";
}

bool checkGroups(QTextStream& out, const QDir& dir)
{
    const QString path = dir.filePath(u"groups.fct"_s);

    const std::expected<tc::TagGroupsFile, tc::LoadError> gf = tc::readTagGroups(path);
    if (!gf) {
        out << "groups.fct: " << gf.error().reason << "\n";
        return false;
    }

    qsizetype facets = 0;
    for (const tc::TagGroup& g : gf->groups.all())
        facets += g.facets.size();

    out << "groups.fct            groups=" << gf->groups.all().size() << " facetRefs=" << facets
        << " header=" << gf->header.size() << " warnings=" << gf->warnings.size()
        << " trailingNL=" << (gf->trailingNewline ? "yes" : "no") << "\n";
    for (qsizetype i = 0; i < gf->warnings.size() && i < kShow; ++i)
        out << "    " << gf->warnings[i].reason << "\n";

    const QString tmp = QDir::temp().filePath(u"tc_groups.tmp"_s);
    if (const std::expected<void, tc::LoadError> w = tc::writeTagGroups(*gf, tmp); !w) {
        out << "    write failed -- " << w.error().reason << "\n";
        return false;
    }
    const QByteArray before = slurp(path);
    const QByteArray after = slurp(tmp);
    QFile::remove(tmp);

    const bool same = before == after;
    out << "    bytes           " << (same ? "identical" : "DIFFER") << "\n";
    if (!same) reportDiff(out, before, after);

    const QList<tc::GroupShadow> shadows = tc::shadowedGroups(gf->groups);
    out << "    shadowed        " << shadows.size() << "\n";
    for (qsizetype i = 0; i < shadows.size() && i < kShow; ++i)
        out << "      " << shadows[i].shadower << " shadows " << shadows[i].shadowed << "\n";
    if (shadows.size() > kShow) out << "      ... and " << (shadows.size() - kShow) << " more\n";

    return same;
}

bool checkVariables(QTextStream& out, const QDir& dir)
{
    const QString path = dir.filePath(u"vars.fct"_s);

    const std::expected<tc::VariablesFile, tc::LoadError> vf = tc::readVariables(path);
    if (!vf) {
        out << "vars.fct: " << vf.error().reason << "\n";
        return false;
    }

    out << "vars.fct              vars=" << vf->vars.all().size()
        << " header=" << vf->header.size() << " warnings=" << vf->warnings.size() << "\n";
    for (const tc::Variable& v : vf->vars.all())
        out << "    $" << v.name << "$ = " << v.value << "\n";
    for (qsizetype i = 0; i < vf->warnings.size() && i < kShow; ++i)
        out << "    " << vf->warnings[i].reason << "\n";

    const QString tmp = QDir::temp().filePath(u"tc_vars.tmp"_s);
    if (const std::expected<void, tc::LoadError> w = tc::writeVariables(*vf, tmp); !w) {
        out << "    write failed -- " << w.error().reason << "\n";
        return false;
    }
    const QByteArray before = slurp(path);
    const QByteArray after = slurp(tmp);
    QFile::remove(tmp);

    const bool same = before == after;
    out << "    bytes           " << (same ? "identical" : "DIFFER") << "\n";
    if (!same) reportDiff(out, before, after);

    // An edited file must survive a write and re-read.
    bool editOk = false;
    {
        tc::VariablesFile edited = *vf;
        const qsizetype was = edited.vars.all().size();
        edited.vars.set(u"tc_probe"_s, u"probe value"_s);

        const bool removed = was > 0 && edited.vars.remove(edited.vars.all().first().name);
        const qsizetype want = was + (removed ? 0 : 1);

        const QString tmp2 = QDir::temp().filePath(u"tc_vars_edit.tmp"_s);
        if (const std::expected<void, tc::LoadError> w = tc::writeVariables(edited, tmp2); !w) {
            out << "    edit write failed -- " << w.error().reason << "\n";
        } else if (const std::expected<tc::VariablesFile, tc::LoadError> back =
                       tc::readVariables(tmp2)) {
            editOk = back->vars.all().size() == want
                && back->vars.value(u"tc_probe"_s) == u"probe value"_s
                && back->header == edited.header;
            out << "    edit round-trip vars=" << back->vars.all().size() << "/" << want
                << (editOk ? "  ok" : "  MISMATCH") << "\n";
        } else {
            out << "    edit reread failed -- " << back.error().reason << "\n";
        }
        QFile::remove(tmp2);
    }

    tc::Variables probe;
    probe.set(u"color"_s, u"black latex"_s);
    probe.set(u"empty"_s, QString());

    struct Case {
        QString in;
        QString want;
    };
    const QList<Case> cases = {
        {u"$color$ dress"_s, u"black latex dress"_s},
        {u"blue $empty$ eyes"_s, u"blue eyes"_s},
        {u"blue $undefined$ eyes"_s, u"blue eyes"_s},
        {u"$color$-trim"_s, u"black latex-trim"_s},
        {u"$empty$leading"_s, u"leading"_s},
        {u"no variables here"_s, u"no variables here"_s},
        {u"$color$ and $color$"_s, u"black latex and black latex"_s},
    };

    qsizetype failed = 0;
    for (const Case& c : cases) {
        const QString got = probe.expand(c.in);
        if (got != c.want) {
            ++failed;
            out << "    expand FAIL  \"" << c.in << "\" -> \"" << got << "\" want \"" << c.want
                << "\"\n";
        }
    }
    out << "    expand cases    " << (cases.size() - failed) << "/" << cases.size() << " ok\n";

    const QStringList probeTags = {u"$color$ dress"_s, u"$colour$ eyes"_s, u"$nope$ $alsono$"_s};
    const QList<tc::VarIssue> issues = tc::undefinedVariables(probeTags, probe);
    out << "    undefined found " << issues.size() << "\n";
    for (const tc::VarIssue& i : issues)
        out << "      $" << i.name << "$ in \"" << i.tag << "\"\n";

    return same && editOk && failed == 0 && issues.size() == 3;
}

bool checkRuleEval(QTextStream& out, const QDir& dir)
{
    const std::expected<tc::TagFacetsFile, tc::LoadError> defs =
        tc::readTagFacets(dir.filePath(u"tag_definitions.fct"_s));
    const std::expected<tc::RuleFile, tc::LoadError> rf =
        tc::readRules(dir.filePath(u"rules.fct"_s));
    if (!defs || !rf) {
        out << "rule eval: prerequisites failed\n";
        return false;
    }

    QList<tc::PipelineTag> input;
    const QStringList tags = defs->defs.definedTags();
    input.reserve(tags.size());
    for (const QString& t : tags) {
        tc::PipelineTag pt;
        pt.tag = t;
        pt.facets = defs->defs.facetsFor(t);
        input << pt;
    }

    const QList<tc::PipelineTag> result = tc::applyRules(rf->rules, input, defs->defs);

    QMap<QString, qsizetype> byResult;
    QMap<QString, qsizetype> byRule;
    for (const tc::PipelineTag& pt : result) {
        ++byResult[resultName(pt.result)];
        if (!pt.ruleSource.isEmpty()) ++byRule[pt.ruleSource];
    }

    out << "rule eval             in=" << input.size() << " out=" << result.size() << "\n";
    for (auto it = byResult.cbegin(); it != byResult.cend(); ++it)
        out << "    " << it.key().leftJustified(13) << it.value() << "\n";
    out << "    fired\n";
    for (auto it = byRule.cbegin(); it != byRule.cend(); ++it)
        out << "      " << it.key().leftJustified(26) << it.value() << "\n";

    return true;
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        out << "usage: tagcomposer <systemDir>\n";
        return 2;
    }

    const QDir dir(args[1]);
    out << "reading " << QFileInfo(dir.path()).absoluteFilePath() << "\n\n";

    const bool facetsOk = checkFacets(out, dir);
    out << "\n";
    const bool rulesOk = checkRules(out, dir);
    out << "\n";
    const bool evalOk = checkRuleEval(out, dir);
    out << "\n";
    const bool varsOk = checkVariables(out, dir);
    out << "\n";
    const bool groupsOk = checkGroups(out, dir);
    out << "\n";
    const bool pipelineOk = checkPipeline(out);
    out << "\n";
    const bool promptOk = checkPrompt(out);
    out << "\n";
    const bool storeOk = checkStore(out);
    out << "\n";
    const bool settingsOk = checkSettings(out, dir);
    out << "\n";
    const bool workflowsOk = checkWorkflows(out, dir);
    out << "\n";
    const bool runOk = checkRun(out);
    out << "\n";
    const bool entryStoreOk = checkEntryStore(out);
    out << "\n";
    const bool searchOk = checkSearch(out);

    if (args.size() >= 3) {
        out << "\n";
        showSearchTiming(out, args[2]);
        out << "\n";
        showRealPrompt(out, dir, args[2]);
    }

    const bool ok =
        facetsOk && rulesOk && evalOk && varsOk && groupsOk && pipelineOk && promptOk && storeOk
        && workflowsOk && runOk && settingsOk
        && entryStoreOk && searchOk;
    out << "\n" << (ok ? u"all checks passed"_s : u"FAILURES above"_s) << "\n";
    return ok ? 0 : 1;
}
