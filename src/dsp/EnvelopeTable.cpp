#include "EnvelopeTable.h"

namespace kick::dsp {

EnvelopeTableBuffer::EnvelopeTableBuffer()
    : tables_(new Table[3])
{
    const Envelope def;
    for (int i = 0; i < 3; ++i)
        render(def, tables_[size_t(i)]);
}

void EnvelopeTableBuffer::render(const Envelope& env, Table& out) noexcept
{
    Envelope e = env;
    e.normalise();
    e.render(out.v, kSize);

    // The last segment ends on the first node sitting at x = 1: that value is approached at the
    // end of the cycle (the table's final point and the one-shot hold value).
    const int n = e.size();
    int j = n - 1;
    while (j > 0 && e.node(j - 1).x >= 1.f)
        --j;
    out.v[kSize] = e.node(j).y;

    // Vertical steps strictly inside the cycle. A step exactly on a cell boundary belongs to the
    // cell that ends there (at = 1), since v[] already holds the right-hand value at the boundary.
    for (int c = 0; c < kSize; ++c)
        out.cellStep[c] = -1;
    int count = 0;
    for (int i = 0; i < n;) {
        int last = i;
        while (last + 1 < n && e.node(last + 1).x == e.node(i).x)
            ++last;
        const float x = e.node(i).x;
        const float pre = e.node(i).y, post = e.node(last).y;
        if (last > i && x > 0.f && x < 1.f && pre != post) {
            const float pos  = x * float(kSize);
            int         cell = int(pos);
            float       at   = pos - float(cell);
            if (at <= 0.f) {
                --cell;
                at = 1.f;
            }
            if (cell >= 0 && cell < kSize && out.cellStep[cell] < 0) {
                out.steps[count]    = Step{at, pre, post};
                out.cellStep[cell] = int16_t(count);
                ++count;
            }
        }
        i = last + 1;
    }
}

void EnvelopeTableBuffer::publish(const Envelope& env)
{
    std::lock_guard<std::mutex> lock(writerMutex_);
    render(env, tables_[back_]);
    const uint32_t prev = middle_.exchange(back_ | kDirty, std::memory_order_acq_rel);
    back_ = prev & ~kDirty;
}

const EnvelopeTableBuffer::Table& EnvelopeTableBuffer::acquire() noexcept
{
    if (middle_.load(std::memory_order_relaxed) & kDirty) {
        const uint32_t prev = middle_.exchange(front_, std::memory_order_acq_rel);
        front_ = prev & ~kDirty;
    }
    return tables_[front_];
}

} // namespace kick::dsp
