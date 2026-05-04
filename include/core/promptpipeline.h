#pragma once
#include <core/facetindex.h>
#include <core/ruleengine.h>
#include <core/variableindex.h>
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
    static QString buildPromptString(const QList<CategoryGroup>& groups, bool forJson);

signals:
    void pipelineReady(QList<core::CategoryGroup> groups);

private:
    FacetIndex* m_facets;
    RuleEngine* m_rules;
    VariableIndex* m_varIndex;

    QList<CategoryGroup> groupByCategory(const QList<PipelineTag>& tags) const;
};

} // namespace core
