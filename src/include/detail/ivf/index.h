/**
 * @file   ivf/index.h
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
 *
 */

#ifndef TILEDB_IVF_IVF_INDEX_H
#define TILEDB_IVF_IVF_INDEX_H

#include <filesystem>
#include <string>
#include <thread>
#include <tiledb/tiledb>
#include <tuple>
#include <vector>

#include <docopt.h>

#include "detail/linalg/tdb_matrix.h"
#include "flat_query.h"
#include "ivf_query.h"
#include "utils/utils.h"

using namespace detail::flat;

namespace detail::ivf {

template <typename T, class ids_type, class centroids_type>
int ivf_index(
    tiledb::Context& ctx,
    const ColMajorMatrix<T>& db,
    const std::string& centroids_uri,
    const std::string& parts_uri,
    const std::string& index_uri,
    const std::string& id_uri,
    size_t start_pos,
    size_t end_pos,
    size_t nthreads) {
  if (nthreads == 0) {
    nthreads = std::thread::hardware_concurrency();
  }
  auto centroids = tdbColMajorMatrix<centroids_type>(ctx, centroids_uri);
  centroids.load();
  auto parts = detail::flat::qv_partition(centroids, db, nthreads);
  debug_matrix(parts, "parts");
  {
    scoped_timer _{"shuffling data"};
    std::vector<size_t> degrees(centroids.num_cols());
    std::vector<ids_type> indices(centroids.num_cols() + 1);
    for (size_t i = 0; i < db.num_cols(); ++i) {
      auto j = parts[i];
      ++degrees[j];
    }
    indices[0] = 0;
    std::inclusive_scan(begin(degrees), end(degrees), begin(indices) + 1);

    std::vector<size_t> check(indices.size());
    std::copy(begin(indices), end(indices), begin(check));

    debug_matrix(degrees, "degrees");
    debug_matrix(indices, "indices");

    // Some variables for debugging
    // @todo remove these once we are confident in the code
    auto mis = std::max_element(begin(indices), end(indices));
    auto a = std::distance(begin(indices), mis);
    auto b = std::distance(mis, end(indices));
    auto misx = *mis;

    // Array for storing the shuffled data
    auto shuffled_db = ColMajorMatrix<T>{db.num_rows(), db.num_cols()};
    std::vector shuffled_ids = std::vector<ids_type>(db.num_cols());
    std::iota(begin(shuffled_ids), end(shuffled_ids), 0);

    debug_matrix(shuffled_db, "shuffled_db");
    debug_matrix(shuffled_ids, "shuffled_ids");

    // @todo parallelize
    // Unfortunately this variant of the algorithm is not parallelizable.
    // The other approach involves doing parallel sort on the indices,
    // which will group them nicely -- but a distributed parallel sort may
    // be difficult to implement.  Even this algorithm is not trivial to
    // parallelize, because of the random access to the indices array.
    for (size_t i = 0; i < db.num_cols(); ++i) {
      size_t bin = parts[i];
      size_t ibin = indices[bin];

      shuffled_ids[ibin] = i + start_pos;

      assert(ibin < shuffled_db.num_cols());
      for (size_t j = 0; j < db.num_rows(); ++j) {
        shuffled_db(j, ibin) = db(j, i);
      }
      ++indices[bin];
    }

    std::shift_right(begin(indices), end(indices), 1);
    indices[0] = 0;

    // A check for debugging
    auto x = std::equal(begin(indices), end(indices), begin(check));

    for (size_t i = 0; i < size(indices); ++i) {
      indices[i] = indices[i] + start_pos;
    }

    // Write out the arrays
    if (parts_uri != "") {
      write_matrix<T, stdx::layout_left, size_t>(
          ctx, shuffled_db, parts_uri, start_pos, false);
    }
    if (index_uri != "") {
      write_vector<ids_type>(ctx, indices, index_uri, 0, false);
    }
    if (id_uri != "") {
      write_vector<ids_type>(ctx, shuffled_ids, id_uri, start_pos, false);
    }
  }
  return 0;
}

template <typename T, class ids_type, class centroids_type>
int ivf_index(
    tiledb::Context& ctx,
    const std::string& db_uri,
    const std::string& centroids_uri,
    const std::string& parts_uri,
    const std::string& index_uri,
    const std::string& id_uri,
    size_t nthreads) {
  return ivf_index<T, ids_type, centroids_type>(
      ctx, db_uri, centroids_uri, parts_uri, index_uri, id_uri, 0, 0, nthreads);
}

template <typename T, class ids_type, class centroids_type>
int ivf_index(
    tiledb::Context& ctx,
    const std::string& db_uri,
    const std::string& centroids_uri,
    const std::string& parts_uri,
    const std::string& index_uri,
    const std::string& id_uri,
    size_t start_pos,
    size_t end_pos,
    size_t nthreads) {
  auto db = tdbColMajorMatrix<T>(ctx, db_uri, 0, 0, start_pos, end_pos);
  db.load();
  return ivf_index<T, ids_type, centroids_type>(
      ctx,
      db,
      centroids_uri,
      parts_uri,
      index_uri,
      id_uri,
      start_pos,
      end_pos,
      nthreads);
}

}  // namespace detail::ivf

#endif  // TILEDB_IVF_IVF_INDEX_H