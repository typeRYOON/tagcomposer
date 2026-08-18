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

    // Facet overrides keyed by the raw (pre-variable-expansion) tag string,
    // owned by the caller. Lets the composer inject a custom tag that carries
    // a group's facets without a tag_definitions.fct entry. nullptr = none.
    void setCustomFacets(const QHash<QString, QList<QString>>* facets)
    {
        m_customFacets = facets;
    }

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
    const QHash<QString, QList<QString>>* m_customFacets = nullptr;

    QList<CategoryGroup> groupByCategory(const QList<PipelineTag>& tags) const;
};

} // namespace core
