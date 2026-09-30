#include <core/composer_doc.h>
#include <algorithm>

namespace tc {

Weight weightOf(const ComposerDoc& doc, const QString& activeTag)
{
    const auto it = doc.weights.constFind(activeTag);
    if (it == doc.weights.constEnd()) return {};
    return Weight{*it, true};
}

Weight mergeWeights(Weight a, Weight b)
{
    if (a.wasSet && b.wasSet) return Weight{std::max(a.value, b.value), true};
    if (a.wasSet) return a;
    if (b.wasSet) return b;
    return {};
}

} // namespace tc
