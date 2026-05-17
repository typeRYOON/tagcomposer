#pragma once
#include <core/facetindex.h>
#include <core/ruleengine.h>
#include <core/variableindex.h>
#include <utils/appsettings.h>
#include <QObject>

namespace core {

class PromptPipeline : public QObject {
    Q_OBJECT
public:
    explicit PromptPipeline(FacetIndex* facets, RuleEngine* rules, VariableIndex* vars = nullptr,
                            QObject* parent = nullptr);

    // Async: emits pipelineReady on completion.
    void push(const QList<QString>& tags);

    // Synchronous variant; used by the batch runner.
    QList<CategoryGroup> evaluate(const QList<QString>& tags) const;

    // Joins Include + Injected tags into a comma-separated prompt string.
    // `formats` wraps each tag whose facets contain a rule's facet name with
    // its prefix/suffix; rules stack in list order. Applied before weight
    // serialization so weighted output becomes `(<prefix>tag<suffix>:1.5)`.
    static QString buildPromptString(const QList<CategoryGroup>& groups, bool forJson,
                                     const QList<utils::FacetFormat>& formats = {});

signals:
    void pipelineReady(QList<core::CategoryGroup> groups);

private:
    FacetIndex* m_facets;
    RuleEngine* m_rules;
    VariableIndex* m_varIndex;

    QList<CategoryGroup> groupByCategory(const QList<PipelineTag>& tags) const;
};

} // namespace core
