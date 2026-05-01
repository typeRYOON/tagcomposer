#pragma once
#include <core/facetindex.h>
#include <core/ruleengine.h>
#include <core/variableindex.h>
#include <QObject>

namespace core {

class PromptPipeline : public QObject {
    Q_OBJECT
public:
    explicit PromptPipeline(
        FacetIndex*    facets,
        RuleEngine*    rules,
        VariableIndex* vars   = nullptr,
        QObject*       parent = nullptr
    );

    // Push the active image's tags through the full pipeline.
    // Emits pipelineReady when done.
    void push(const QList<QString>& tags);

    // Same logic as push() but synchronous — returns the resulting groups
    // directly without emitting. Used by the batch runner to compute prompts
    // for arbitrary entry tag sets without disturbing composer state.
    QList<CategoryGroup> evaluate(const QList<QString>& tags) const;

    // Formats the groups into a prompt string (only Include + Injected tags).
    static QString buildPromptString(const QList<CategoryGroup>& groups, bool forJson);

signals:
    void pipelineReady(QList<core::CategoryGroup> groups);

private:
    FacetIndex*    m_facets;
    RuleEngine*    m_rules;
    VariableIndex* m_varIndex;

    QList<CategoryGroup> groupByCategory(const QList<PipelineTag>& tags) const;
};

} // namespace core
