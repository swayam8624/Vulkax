#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace vulkax::numerics {

class DenseMatrix {
public:
    DenseMatrix() = default;
    DenseMatrix(std::size_t rows, std::size_t cols, double initial = 0.0);

    [[nodiscard]] std::size_t rows() const noexcept { return rows_; }
    [[nodiscard]] std::size_t cols() const noexcept { return cols_; }

    double& operator()(std::size_t row, std::size_t col);
    [[nodiscard]] double operator()(std::size_t row, std::size_t col) const;

    [[nodiscard]] static DenseMatrix identity(std::size_t size);
    [[nodiscard]] DenseMatrix transposed() const;
    [[nodiscard]] std::vector<double> multiply(std::span<const double> vector) const;

private:
    std::size_t rows_{};
    std::size_t cols_{};
    std::vector<double> data_;
};

[[nodiscard]] double dot(std::span<const double> lhs, std::span<const double> rhs);
[[nodiscard]] double l2Norm(std::span<const double> values);
[[nodiscard]] std::vector<double> solveGaussian(DenseMatrix matrix, std::vector<double> rhs,
                                                double pivotTolerance = 1.0e-12);

} // namespace vulkax::numerics
