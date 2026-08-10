#include "vulkax/numerics/dense.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace vulkax::numerics {

DenseMatrix::DenseMatrix(std::size_t rows, std::size_t cols, double initial)
    : rows_(rows), cols_(cols), data_(rows * cols, initial) {}

double& DenseMatrix::operator()(std::size_t row, std::size_t col) {
    if (row >= rows_ || col >= cols_) {
        throw std::out_of_range("DenseMatrix index out of range");
    }
    return data_[row * cols_ + col];
}

double DenseMatrix::operator()(std::size_t row, std::size_t col) const {
    if (row >= rows_ || col >= cols_) {
        throw std::out_of_range("DenseMatrix index out of range");
    }
    return data_[row * cols_ + col];
}

DenseMatrix DenseMatrix::identity(std::size_t size) {
    DenseMatrix result(size, size);
    for (std::size_t i = 0; i < size; ++i) {
        result(i, i) = 1.0;
    }
    return result;
}

DenseMatrix DenseMatrix::transposed() const {
    DenseMatrix result(cols_, rows_);
    for (std::size_t row = 0; row < rows_; ++row) {
        for (std::size_t col = 0; col < cols_; ++col) {
            result(col, row) = (*this)(row, col);
        }
    }
    return result;
}

std::vector<double> DenseMatrix::multiply(std::span<const double> vector) const {
    if (vector.size() != cols_) {
        throw std::invalid_argument("matrix/vector dimension mismatch");
    }
    std::vector<double> result(rows_, 0.0);
    for (std::size_t row = 0; row < rows_; ++row) {
        for (std::size_t col = 0; col < cols_; ++col) {
            result[row] += (*this)(row, col) * vector[col];
        }
    }
    return result;
}

double dot(std::span<const double> lhs, std::span<const double> rhs) {
    if (lhs.size() != rhs.size()) {
        throw std::invalid_argument("dot-product dimension mismatch");
    }
    double result = 0.0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        result += lhs[i] * rhs[i];
    }
    return result;
}

double l2Norm(std::span<const double> values) { return std::sqrt(dot(values, values)); }

std::vector<double> solveGaussian(DenseMatrix matrix, std::vector<double> rhs,
                                  double pivotTolerance) {
    if (matrix.rows() != matrix.cols() || rhs.size() != matrix.rows()) {
        throw std::invalid_argument("solveGaussian requires a square matrix and matching rhs");
    }

    const std::size_t n = matrix.rows();
    for (std::size_t pivot = 0; pivot < n; ++pivot) {
        std::size_t best = pivot;
        double bestMagnitude = std::abs(matrix(pivot, pivot));
        for (std::size_t row = pivot + 1; row < n; ++row) {
            const double magnitude = std::abs(matrix(row, pivot));
            if (magnitude > bestMagnitude) {
                best = row;
                bestMagnitude = magnitude;
            }
        }
        if (bestMagnitude <= pivotTolerance) {
            throw std::runtime_error("singular or ill-conditioned dense system");
        }
        if (best != pivot) {
            for (std::size_t col = pivot; col < n; ++col) {
                std::swap(matrix(pivot, col), matrix(best, col));
            }
            std::swap(rhs[pivot], rhs[best]);
        }

        const double diagonal = matrix(pivot, pivot);
        for (std::size_t row = pivot + 1; row < n; ++row) {
            const double factor = matrix(row, pivot) / diagonal;
            matrix(row, pivot) = 0.0;
            for (std::size_t col = pivot + 1; col < n; ++col) {
                matrix(row, col) -= factor * matrix(pivot, col);
            }
            rhs[row] -= factor * rhs[pivot];
        }
    }

    std::vector<double> solution(n, 0.0);
    for (std::size_t reverse = 0; reverse < n; ++reverse) {
        const std::size_t row = n - 1 - reverse;
        double value = rhs[row];
        for (std::size_t col = row + 1; col < n; ++col) {
            value -= matrix(row, col) * solution[col];
        }
        solution[row] = value / matrix(row, row);
    }
    return solution;
}

} // namespace vulkax::numerics
