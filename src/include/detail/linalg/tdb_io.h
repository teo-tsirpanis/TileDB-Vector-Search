/**
 * @file   tdb_io.h
 *
 * @section LICENSE
 *
 * The MIT License
 *
 * @copyright Copyright (c) 2023 TileDB, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * @section DESCRIPTION
 *
 */

#ifndef TILEDB_TDB_IO_H
#define TILEDB_TDB_IO_H

#include <numeric>
#include <vector>

#include <tiledb/tiledb>
#include "detail/linalg/matrix.h"
#include "utils/logging.h"
#include "utils/timer.h"

template <class T, class LayoutPolicy = stdx::layout_right, class I = size_t>
void create_matrix(
    const tiledb::Context& ctx,
    const Matrix<T, LayoutPolicy, I>& A,
    const std::string& uri) {
  if (global_debug) {
    std::cerr << "# Creating Matrix: " << uri << std::endl;
  }

  // @todo: make this a parameter
  size_t num_parts = 10;
  size_t row_extent = std::max<size_t>(
      (A.num_rows() + num_parts - 1) / num_parts, A.num_rows() >= 2 ? 2 : 1);
  size_t col_extent = std::max<size_t>(
      (A.num_cols() + num_parts - 1) / num_parts, A.num_cols() >= 2 ? 2 : 1);

  tiledb::Domain domain(ctx);
  domain
      .add_dimension(tiledb::Dimension::create<int>(
          ctx, "rows", {{0, (int)A.num_rows() - 1}}, row_extent))
      .add_dimension(tiledb::Dimension::create<int>(
          ctx, "cols", {{0, (int)A.num_cols() - 1}}, col_extent));

  // The array will be dense.
  tiledb::ArraySchema schema(ctx, TILEDB_DENSE);

  auto order = std::is_same_v<LayoutPolicy, stdx::layout_right> ?
                   TILEDB_ROW_MAJOR :
                   TILEDB_COL_MAJOR;
  schema.set_domain(domain).set_order({{order, order}});

  schema.add_attribute(tiledb::Attribute::create<T>(ctx, "values"));

  tiledb::Array::create(uri, schema);
}
/**
 * Write the contents of a Matrix to a TileDB array.
 */
template <class T, class LayoutPolicy = stdx::layout_right, class I = size_t>
void write_matrix(
    const tiledb::Context& ctx,
    const Matrix<T, LayoutPolicy, I>& A,
    const std::string& uri,
    size_t start_pos = 0,
    bool create = true) {
  scoped_timer _{tdb_func__ + " " + std::string{uri}};
  if (global_debug) {
    std::cerr << "# Writing Matrix: " << uri << std::endl;
  }

  if (create) {
    create_matrix<T, LayoutPolicy, I>(ctx, A, uri);
  }
  std::vector<int32_t> subarray_vals{
      0,
      (int)A.num_rows() - 1,
      (int)start_pos,
      (int)start_pos + (int)A.num_cols() - 1};

  // Open array for writing
  tiledb::Array array =
      tiledb_helpers::open_array(tdb_func__, ctx, uri, TILEDB_WRITE);

  tiledb::Subarray subarray(ctx, array);
  subarray.set_subarray(subarray_vals);

  tiledb::Query query(ctx, array);
  auto order = std::is_same_v<LayoutPolicy, stdx::layout_right> ?
                   TILEDB_ROW_MAJOR :
                   TILEDB_COL_MAJOR;
  query.set_layout(order)
      .set_data_buffer(
          "values", &A(0, 0), (uint64_t)A.num_rows() * (uint64_t)A.num_cols())
      .set_subarray(subarray);
  tiledb_helpers::submit_query(tdb_func__, uri, query);

  assert(tiledb::Query::Status::COMPLETE == query.query_status());

  array.close();
}

template <class T>
void create_vector(
    const tiledb::Context& ctx, std::vector<T>& v, const std::string& uri) {
  if (global_debug) {
    std::cerr << "# Creating std::vector: " << uri << std::endl;
  }

  size_t num_parts = 10;
  size_t tile_extent = (size(v) + num_parts - 1) / num_parts;
  tiledb::Domain domain(ctx);
  domain.add_dimension(tiledb::Dimension::create<int>(
      ctx, "rows", {{0, (int)size(v) - 1}}, tile_extent));

  // The array will be dense.
  tiledb::ArraySchema schema(ctx, TILEDB_DENSE);
  schema.set_domain(domain).set_order({{TILEDB_ROW_MAJOR, TILEDB_ROW_MAJOR}});

  schema.add_attribute(tiledb::Attribute::create<T>(ctx, "values"));

  tiledb::Array::create(uri, schema);
}

/**
 * Write the contents of a std::vector to a TileDB array.
 * @todo change the naming of this function to something more appropriate
 */
template <class T>
void write_vector(
    const tiledb::Context& ctx,
    std::vector<T>& v,
    const std::string& uri,
    size_t start_pos = 0,
    bool create = true) {
  scoped_timer _{tdb_func__ + " " + std::string{uri}};

  if (global_debug) {
    std::cerr << "# Writing std::vector: " << uri << std::endl;
  }

  if (create) {
    create_vector<T>(ctx, v, uri);
  }
  // Set the subarray to write into
  std::vector<int32_t> subarray_vals{
      (int)start_pos, (int)start_pos + (int)size(v) - 1};

  // Open array for writing
  tiledb::Array array =
      tiledb_helpers::open_array(tdb_func__, ctx, uri, TILEDB_WRITE);

  tiledb::Subarray subarray(ctx, array);
  subarray.set_subarray(subarray_vals);

  tiledb::Query query(ctx, array);
  query.set_layout(TILEDB_ROW_MAJOR)
      .set_data_buffer("values", v)
      .set_subarray(subarray);

  query.submit();
  assert(tiledb::Query::Status::COMPLETE == query.query_status());
  tiledb_helpers::submit_query(tdb_func__, uri, query);

  array.close();
}

/**
 * Read the contents of a TileDB array into a std::vector.
 */
template <class T>
std::vector<T> read_vector(const tiledb::Context& ctx, const std::string& uri) {
  scoped_timer _{tdb_func__ + " " + std::string{uri}};

  if (global_debug) {
    std::cerr << "# Reading std::vector: " << uri << std::endl;
  }

  tiledb::Array array_ =
      tiledb_helpers::open_array(tdb_func__, ctx, uri, TILEDB_READ);
  auto schema_ = array_.schema();

  using domain_type = int32_t;
  const size_t idx = 0;

  auto domain_{schema_.domain()};

  auto dim_num_{domain_.ndim()};
  auto array_rows_{domain_.dimension(0)};

  auto vec_rows_{
      (array_rows_.template domain<domain_type>().second -
       array_rows_.template domain<domain_type>().first + 1)};

  auto attr_num{schema_.attribute_num()};
  auto attr = schema_.attribute(idx);

  std::string attr_name = attr.name();
  tiledb_datatype_t attr_type = attr.type();

  // Create a subarray that reads the array up to the specified subset.
  std::vector<int32_t> subarray_vals = {0, vec_rows_ - 1};
  tiledb::Subarray subarray(ctx, array_);
  subarray.set_subarray(subarray_vals);

  // @todo: use something non-initializing
  std::vector<T> data_(vec_rows_);

  tiledb::Query query(ctx, array_);
  query.set_subarray(subarray).set_data_buffer(
      attr_name, data_.data(), vec_rows_);
  tiledb_helpers::submit_query(tdb_func__, uri, query);
  _memory_data.insert_entry(tdb_func__, vec_rows_ * sizeof(T));

  array_.close();
  assert(tiledb::Query::Status::COMPLETE == query.query_status());

  return data_;
}

template <class T>
auto sizes_to_indices(const std::vector<T>& sizes) {
  std::vector<T> indices(size(sizes) + 1);
  std::inclusive_scan(begin(sizes), end(sizes), begin(indices) + 1);

  return indices;
}

#endif  // TILEDB_TDB_IO_H