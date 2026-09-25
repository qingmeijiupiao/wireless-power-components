/*
 * @version: no version
 * @LastEditors: qingmeijiupiao
 * @Description: 插值模块, 提供等间隔插值和单调插值两种插值方式
 * @author: qingmeijiupiao
 * @LastEditTime: 2026-06-01 18:36:30
 */
#include <vector>
#include <cmath>
#include <algorithm>
#include <utility>
#include <cassert>

// 基类：插值接口
template <typename InputType, typename OutputType> class InterpBase {
  public:
    enum class Monotonicity { INCREASING, DECREASING };

    /** @brief `~InterpBase` 接口。 */
    virtual ~InterpBase() = default;

    // 纯虚插值函数
    /** @brief `interpolate` 接口。 */
    virtual OutputType interpolate(InputType x) const noexcept = 0;

    // 获取输入范围
    /** @brief `getMinInput` 接口。 */
    virtual InputType getMinInput() const noexcept = 0;
    /** @brief `getMaxInput` 接口。 */
    virtual InputType getMaxInput() const noexcept = 0;

  protected:
    /** @brief `InterpBase` 接口。 */
    InterpBase() = default;
    OutputType map_value(InputType input, InputType Ileft, InputType Iright, OutputType Oleft,
                         OutputType Oright) const noexcept {
        return Oleft + (Oright - Oleft) * (input - Ileft) / (Iright - Ileft);
    }
};

// 派生类：等间隔插值（通过索引快速查找）
template <typename InputType, typename OutputType> class EquidistantInterp : public InterpBase<InputType, OutputType> {
  public:
    // 构造函数：接受二维点列表，自动推导等间隔参数
    /** @brief `EquidistantInterp` 接口。 */
    explicit EquidistantInterp(const std::vector<std::pair<InputType, OutputType>>& points) noexcept : values() {
        assert(points.size() >= 2 && "EquidistantInterp: at least 2 points required");

        start = points[0].first;
        step  = points[1].first - points[0].first;
        assert(step != InputType(0) && "EquidistantInterp: step cannot be zero");

        values.reserve(points.size());
        for (const auto& p : points) {
            values.push_back(p.second);
        }

        // 自动确定单调性
        if (step > InputType(0)) {
            monotonicity = InterpBase<InputType, OutputType>::Monotonicity::INCREASING;
        } else {
            monotonicity = InterpBase<InputType, OutputType>::Monotonicity::DECREASING;
        }
    }

    /** @brief `interpolate` 接口。 */
    OutputType interpolate(InputType x) const noexcept override {
        // 计算输入范围
        InputType firstInput = start;
        InputType lastInput  = start + step * (values.size() - 1);

        // 边界处理
        if (monotonicity == InterpBase<InputType, OutputType>::Monotonicity::INCREASING) {
            if (x <= firstInput)
                return values.front();
            if (x >= lastInput)
                return values.back();
        } else { // DECREASING
            if (x >= firstInput)
                return values.front();
            if (x <= lastInput)
                return values.back();
        }

        // 计算浮点索引
        float index_d = static_cast<float>(x - start) / static_cast<float>(step);
        int   i       = static_cast<int>(std::floor(index_d));

        // 索引保护（防止浮点误差）
        if (i < 0)
            i = 0;
        if (i >= static_cast<int>(values.size()) - 1)
            i = values.size() - 2;

        OutputType y0 = values[i];
        OutputType y1 = values[i + 1];
        return this->map_value(x, start + i * step, start + (i + 1) * step, y0, y1);
    }

    /** @brief `getMinInput` 接口。 */
    InputType getMinInput() const noexcept override {
        return step > InputType(0) ? start : start + step * (values.size() - 1);
    }

    /** @brief `getMaxInput` 接口。 */
    InputType getMaxInput() const noexcept override {
        return step > InputType(0) ? start + step * (values.size() - 1) : start;
    }

  private:
    InputType                                                start;
    InputType                                                step;
    std::vector<OutputType>                                  values;
    typename InterpBase<InputType, OutputType>::Monotonicity monotonicity;
};

// 派生类：不等间隔插值（二分查找）
template <typename InputType, typename OutputType>
class NonEquidistantInterp : public InterpBase<InputType, OutputType> {
  public:
    // 构造函数：接受二维点列表，自动推导单调性
    explicit NonEquidistantInterp(const std::vector<std::pair<InputType, OutputType>>& points) noexcept
        : inputs(), outputs() {
        assert(points.size() >= 2 && "NonEquidistantInterp: at least 2 points required");

        inputs.reserve(points.size());
        outputs.reserve(points.size());
        for (const auto& p : points) {
            inputs.push_back(p.first);
            outputs.push_back(p.second);
        }

        // 推导单调性
        if (inputs.front() < inputs.back()) {
            monotonicity = InterpBase<InputType, OutputType>::Monotonicity::INCREASING;
        } else if (inputs.front() > inputs.back()) {
            monotonicity = InterpBase<InputType, OutputType>::Monotonicity::DECREASING;
        } else {
            // 严格单调要求首尾不等
            assert(false && "NonEquidistantInterp: inputs must be strictly monotonic");
        }
    }

    /** @brief `interpolate` 接口。 */
    OutputType interpolate(InputType x) const noexcept override {
        // 边界处理
        if (monotonicity == InterpBase<InputType, OutputType>::Monotonicity::INCREASING) {
            if (x <= inputs.front())
                return outputs.front();
            if (x >= inputs.back())
                return outputs.back();
        } else { // DECREASING
            if (x >= inputs.front())
                return outputs.front();
            if (x <= inputs.back())
                return outputs.back();
        }

        int lo = 0, hi = inputs.size() - 1;
        if (monotonicity == InterpBase<InputType, OutputType>::Monotonicity::INCREASING) {
            // 递增：查找第一个 >= x 的位置
            while (lo < hi) {
                int mid = (lo + hi) / 2;
                if (inputs[mid] < x) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            int i = lo - 1;
            if (i < 0)
                i = 0;
            if (i >= static_cast<int>(inputs.size()) - 1)
                i = inputs.size() - 2;

            InputType  x1 = inputs[i];
            InputType  x2 = inputs[i + 1];
            OutputType y1 = outputs[i];
            OutputType y2 = outputs[i + 1];
            return this->map_value(x, x1, x2, y1, y2);
        } else {
            // 递减：查找第一个 <= x 的位置
            while (lo < hi) {
                int mid = (lo + hi) / 2;
                if (inputs[mid] > x) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }
            int i = lo - 1;
            if (i < 0)
                i = 0;
            if (i >= static_cast<int>(inputs.size()) - 1)
                i = inputs.size() - 2;

            InputType  x1 = inputs[i];
            InputType  x2 = inputs[i + 1];
            OutputType y1 = outputs[i];
            OutputType y2 = outputs[i + 1];
            return this->map_value(x, x1, x2, y1, y2);
        }
    }

    /** @brief `getMinInput` 接口。 */
    InputType getMinInput() const noexcept override {
        return monotonicity == InterpBase<InputType, OutputType>::Monotonicity::INCREASING ? inputs.front()
                                                                                           : inputs.back();
    }

    /** @brief `getMaxInput` 接口。 */
    InputType getMaxInput() const noexcept override {
        return monotonicity == InterpBase<InputType, OutputType>::Monotonicity::INCREASING ? inputs.back()
                                                                                           : inputs.front();
    }

  private:
    std::vector<InputType>                                   inputs;
    std::vector<OutputType>                                  outputs;
    typename InterpBase<InputType, OutputType>::Monotonicity monotonicity;
};
