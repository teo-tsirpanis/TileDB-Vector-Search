import os

import numpy as np

import tiledb


def xbin_mmap(fname, dtype):
    n, d = map(int, np.fromfile(fname, dtype="uint32", count=2))
    assert os.stat(fname).st_size == 8 + n * d * np.dtype(dtype).itemsize
    return np.memmap(fname, dtype=dtype, mode="r", offset=8, shape=(n, d))


def get_queries_fvec(file, dimensions, nqueries=None):
    vfs = tiledb.VFS()
    vector_values = 1 + dimensions
    vector_size = vector_values * 4
    read_size = nqueries
    read_offset = 0
    with vfs.open(file, "rb") as f:
        f.seek(read_offset)
        return np.delete(
            np.reshape(
                np.frombuffer(
                    f.read(read_size * vector_size),
                    count=read_size * vector_values,
                    dtype=np.float32,
                ).astype(np.float32),
                (read_size, dimensions + 1),
            ),
            0,
            axis=1,
        )


def get_groundtruth_ivec(file, k=None, nqueries=None):
    vfs = tiledb.VFS()
    vector_values = 1 + k
    vector_size = vector_values * 4
    read_size = nqueries
    read_offset = 0
    with vfs.open(file, "rb") as f:
        f.seek(read_offset)
        return (
            np.delete(
                np.reshape(
                    np.frombuffer(
                        f.read(read_size * vector_size),
                        count=read_size * vector_values,
                        dtype=np.int32,
                    ).astype(np.int32),
                    (read_size, k + 1),
                ),
                0,
                axis=1,
            ),
            None,
        )


def get_queries(dataset_dir, dtype, nqueries=None):
    fname = os.path.join(dataset_dir, "queries")
    x = xbin_mmap(fname, dtype=dtype)
    if nqueries is not None:
        return x.astype(np.float32)[:nqueries, :]
    else:
        return x.astype(np.float32)


def get_groundtruth(dataset_dir, k=None, nqueries=None):
    I, D = groundtruth_read(dataset_dir, nqueries)
    if k is not None:
        assert k <= 100
        I = I[:, :k]
        D = D[:, :k]
    return I, D


def groundtruth_read(dataset_dir, nqueries=None):
    fname = os.path.join(dataset_dir, "gt")
    n, d = map(int, np.fromfile(fname, dtype="uint32", count=2))
    assert os.stat(fname).st_size == 8 + n * d * (4 + 4)
    f = open(fname, "rb")
    f.seek(4 + 4)
    I = np.fromfile(f, dtype="uint32", count=n * d).reshape(n, d).astype("uint64")
    D = np.fromfile(f, dtype="float32", count=n * d).reshape(n, d)

    if nqueries is not None:
        return I[:nqueries, :], D[:nqueries, :]
    else:
        return I, D


def create_random_dataset_f32(nb, d, nq, k, path):
    import sklearn.model_selection
    from sklearn.datasets import make_blobs
    from sklearn.neighbors import NearestNeighbors

    # print(f"Preparing datasets with {nb} random points and {nq} queries.")
    os.mkdir(path)
    X, _ = make_blobs(n_samples=nb + nq, n_features=d, centers=nq, random_state=1)

    data, queries = sklearn.model_selection.train_test_split(
        X, test_size=nq, random_state=1
    )

    with open(os.path.join(path, "data"), "wb") as f:
        np.array([nb, d], dtype="uint32").tofile(f)
        data.astype("float32").tofile(f)
    with open(os.path.join(path, "queries"), "wb") as f:
        np.array([nq, d], dtype="uint32").tofile(f)
        queries.astype("float32").tofile(f)

    # print("Computing groundtruth")

    nbrs = NearestNeighbors(n_neighbors=k, metric="euclidean", algorithm="brute").fit(
        data
    )
    D, I = nbrs.kneighbors(queries)
    with open(os.path.join(path, "gt"), "wb") as f:
        np.array([nq, k], dtype="uint32").tofile(f)
        I.astype("uint32").tofile(f)
        D.astype("float32").tofile(f)


def create_random_dataset_u8(nb, d, nq, k, path):
    import sklearn.model_selection
    from sklearn.datasets import make_blobs
    from sklearn.neighbors import NearestNeighbors

    # print(f"Preparing datasets with {nb} random points and {nq} queries.")
    os.mkdir(path)
    X, _ = make_blobs(n_samples=nb + nq, n_features=d, centers=nq, random_state=1)

    data, queries = sklearn.model_selection.train_test_split(
        X, test_size=nq, random_state=1
    )
    data = data.astype("uint8")
    queries = queries.astype("uint8")

    with open(os.path.join(path, "data"), "wb") as f:
        np.array([nb, d], dtype="uint32").tofile(f)
        data.tofile(f)
    with open(os.path.join(path, "queries"), "wb") as f:
        np.array([nq, d], dtype="uint32").tofile(f)
        queries.tofile(f)

    # print("Computing groundtruth")

    nbrs = NearestNeighbors(n_neighbors=k, metric="euclidean", algorithm="brute").fit(
        data
    )
    D, I = nbrs.kneighbors(queries)
    with open(os.path.join(path, "gt"), "wb") as f:
        np.array([nq, k], dtype="uint32").tofile(f)
        I.astype("uint32").tofile(f)
        D.astype("float32").tofile(f)


def create_schema():
    schema = tiledb.ArraySchema(
        domain=tiledb.Domain(
            *[
                tiledb.Dim(name="__dim_0", domain=(0, 2), tile=3, dtype="int32"),
                tiledb.Dim(name="__dim_1", domain=(0, 3), tile=3, dtype="int32"),
            ]
        ),
        attrs=[
            tiledb.Attr(name="", dtype="float32", var=False, nullable=False),
        ],
        cell_order="col-major",
        tile_order="col-major",
        capacity=10000,
        sparse=False,
    )
    return schema


def create_array(path: str, data):
    schema = create_schema()
    tiledb.Array.create(path, schema)
    with tiledb.open(path, "w") as A:
        A[:] = data


def accuracy(result, gt):
    found = 0
    total = 0
    for i in range(len(result)):
        total += len(result[i])
        found += len(np.intersect1d(result[i], gt[i]))
    return found / total
