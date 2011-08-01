/*
 * Copyright © 2008-2011  Peter Colberg and Felix Höfling
 *
 * This file is part of h5xx.
 *
 * h5xx is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef H5XX_DATASET_HPP
#define H5XX_DATASET_HPP

#include <boost/array.hpp>
#include <boost/mpl/and.hpp>
#include <boost/multi_array.hpp>
#include <boost/type_traits.hpp>
#include <boost/utility/enable_if.hpp>
#include <vector>

#include <h5xx/attribute.hpp>
#include <h5xx/property.hpp>
#include <h5xx/utility.hpp>

namespace h5xx {

/**
 * determine whether dataset exists in file or group
 */
inline bool exists_dataset(H5::CommonFG const& fg, std::string const& name)
{
    H5::IdComponent const& loc(dynamic_cast<H5::IdComponent const&>(fg));
    hid_t hid;
    H5E_BEGIN_TRY {
        hid = H5Dopen(loc.getId(), name.c_str(), H5P_DEFAULT);
        if (hid > 0) {
            H5Dclose(hid);
        }
    } H5E_END_TRY
    return (hid > 0);
}

enum { compression_level = 6 };

/**
 * create dataset 'name' in given group/file with given size
 *
 * This function creates missing intermediate groups.
 */
// generic case: some fundamental type and a shape of arbitrary rank
template <typename T, int rank>
typename boost::enable_if<boost::is_fundamental<T>, H5::DataSet>::type
create_dataset(
    H5::CommonFG const& fg
  , std::string const& name
  , hsize_t const* shape
  , hsize_t max_size=H5S_UNLIMITED)
{
    H5::IdComponent const& loc = dynamic_cast<H5::IdComponent const&>(fg);

    // file dataspace holding max_size multi_array chunks of fixed rank
    boost::array<hsize_t, rank+1> dim, max_dim, chunk_dim;
    std::copy(shape, shape + rank, dim.begin() + 1);
    std::copy(shape, shape + rank, max_dim.begin() + 1);
    std::copy(shape, shape + rank, chunk_dim.begin() + 1);
    dim[0] = (max_size == H5S_UNLIMITED) ? 0 : max_size;
    max_dim[0] = max_size;
    chunk_dim[0] = 1;

    H5::DataSpace dataspace(dim.size(), &dim.front(), &max_dim.front());
    H5::DSetCreatPropList cparms;
    cparms.setChunk(chunk_dim.size(), &chunk_dim.front());
    cparms.setDeflate(compression_level);    // enable GZIP compression

    // remove dataset if it exists
    H5E_BEGIN_TRY {
        H5Ldelete(loc.getId(), name.c_str(), H5P_DEFAULT);
    } H5E_END_TRY

    H5::PropList pl = create_intermediate_group_property();
    hid_t dataset_id = H5Dcreate(
        loc.getId(), name.c_str(), ctype<T>::hid(), dataspace.getId()
      , pl.getId(), cparms.getId(), H5P_DEFAULT
    );
    if (dataset_id < 0) {
        throw error("failed to create dataset \"" + name + "\"");
    }
    return H5::DataSet(dataset_id);
}

/**
 * Create unique dataset 'name' in given group/file. The dataset contains
 * a single entry only and should be written via write_unique_dataset().
 *
 * This function creates missing intermediate groups.
 */
// generic case: some fundamental type and a shape of arbitrary rank
template <typename T, int rank>
typename boost::enable_if<boost::is_fundamental<T>, H5::DataSet>::type
create_unique_dataset(
    H5::CommonFG const& fg
  , std::string const& name
  , hsize_t const* shape)
{
    H5::IdComponent const& loc = dynamic_cast<H5::IdComponent const&>(fg);

    // file dataspace holding a single multi_array of fixed rank
    H5::DataSpace dataspace(rank, shape);
    H5::DSetCreatPropList cparms;
    if (rank > 0 && sizeof(T) * shape[0] > 64) { // enable GZIP compression for at least 64 bytes
        cparms.setChunk(rank, shape);
        cparms.setDeflate(compression_level);
    }

    // remove dataset if it exists
    H5E_BEGIN_TRY {
        H5Ldelete(loc.getId(), name.c_str(), H5P_DEFAULT);
    } H5E_END_TRY

    H5::PropList pl = create_intermediate_group_property();
    hid_t dataset_id = H5Dcreate(
        loc.getId(), name.c_str(), ctype<T>::hid(), dataspace.getId()
      , pl.getId(), cparms.getId(), H5P_DEFAULT
    );
    if (dataset_id < 0) {
        throw error("failed to create dataset \"" + name + "\"");
    }
    return H5::DataSet(dataset_id);
}

/**
 * write data to dataset at given index, default argument appends dataset
 */
// generic case: some fundamental type and a pointer to the contiguous array of data
// size and shape are taken from the dataset
template <typename T, int rank>
typename boost::enable_if<boost::is_fundamental<T>, void>::type
write_dataset(H5::DataSet const& dataset, T const* data, hsize_t index=H5S_UNLIMITED)
{
    H5::DataSpace dataspace(dataset.getSpace());
    if (!has_rank<rank+1>(dataspace)) {
        throw std::runtime_error("HDF5 writer: dataset has incompatible dataspace");
    }

    // select hyperslab of multi_array chunk
    boost::array<hsize_t, rank+1> dim, count, start, stride, block;
    dataspace.getSimpleExtentDims(&dim.front());
    std::fill(count.begin(), count.end(), 1);
    start[0] = dim[0];
    std::fill(start.begin() + 1, start.end(), 0);
    std::fill(stride.begin(), stride.end(), 1);
    block = dim;
    block[0] = 1;

    if (index == H5S_UNLIMITED) {
        // extend dataspace to append another chunk
        dim[0]++;
        dataspace.setExtentSimple(dim.size(), &dim.front());
        try {
            H5XX_NO_AUTO_PRINT(H5::DataSetIException);
            dataset.extend(&dim.front());
        }
        catch (H5::DataSetIException const&) {
            throw std::runtime_error("HDF5 writer: fixed-size dataset cannot be extended");
        }
    }
    else {
        start[0] = index;
    }
    dataspace.selectHyperslab(H5S_SELECT_SET, &count.front(), &start.front(), &stride.front(), &block.front());

    // memory dataspace
    H5::DataSpace mem_dataspace(rank, block.begin() + 1);

    dataset.write(data, ctype<T>::hid(), mem_dataspace, dataspace);
}

/**
 * write data to unique dataset
 */
// generic case: some fundamental type and a pointer to the contiguous array of data
// size and shape are taken from the dataset
template <typename T, int rank>
typename boost::enable_if<boost::is_fundamental<T>, void>::type
write_unique_dataset(H5::DataSet const& dataset, T const* data)
{
    H5::DataSpace dataspace(dataset.getSpace());
    if (!has_rank<rank>(dataspace)) {
        throw std::runtime_error("HDF5 writer: dataset has incompatible dataspace");
    }

    // memory dataspace
    hsize_t dim[rank];
    dataspace.getSimpleExtentDims(dim);
    H5::DataSpace mem_dataspace(rank, dim);

    dataset.write(data, ctype<T>::hid(), mem_dataspace, dataspace);
}

/**
 * read data from dataset at given index
 */
// generic case: some (fundamental) type and a pointer to the contiguous array of data
// size and shape are taken from the dataset
template <typename T, int rank>
typename boost::enable_if<boost::is_fundamental<T>, hsize_t>::type
read_dataset(H5::DataSet const& dataset, T* data, ssize_t index)
{
    H5::DataSpace dataspace(dataset.getSpace());
    if (!has_rank<rank+1>(dataspace)) {
        throw std::runtime_error("HDF5 reader: dataset has incompatible dataspace");
    }

    boost::array<hsize_t, rank+1> dim;
    dataspace.getSimpleExtentDims(&dim.front());

    ssize_t const len = dim[0];
    if ((index >= len) || ((-index) > len)) {
        throw std::runtime_error("HDF5 reader: index out of bounds");
    }
    index = (index < 0) ? (index + len) : index;

    boost::array<hsize_t, rank+1> count, start, stride, block;
    std::fill(count.begin(), count.end(), 1);
    start[0] = index;
    std::fill(start.begin() + 1, start.end(), 0);
    std::fill(stride.begin(), stride.end(), 1);
    block = dim;
    block[0] = 1;

    dataspace.selectHyperslab(H5S_SELECT_SET, &count.front(), &start.front(), &stride.front(), &block.front());

    // memory dataspace
    H5::DataSpace mem_dataspace(rank, dim.begin() + 1);

    try {
        H5XX_NO_AUTO_PRINT(H5::Exception);
        dataset.read(data, ctype<T>::hid(), mem_dataspace, dataspace);
    }
    catch (H5::Exception const&) {
        throw std::runtime_error("HDF5 reader: failed to read multidimensional array data");
    }

    return index;
}

/**
 * read data from unique dataset
 */
// generic case: some (fundamental) type and a pointer to the contiguous array of data
// size and shape are taken from the dataset
template <typename T, int rank>
typename boost::enable_if<boost::is_fundamental<T>, void>::type
read_unique_dataset(H5::DataSet const& dataset, T* data)
{
    H5::DataSpace dataspace(dataset.getSpace());
    if (!has_rank<rank>(dataspace)) {
        throw std::runtime_error("HDF5 reader: dataset has incompatible dataspace");
    }

    // memory dataspace
    hsize_t dim[rank];
    dataspace.getSimpleExtentDims(dim);
    H5::DataSpace mem_dataspace(rank, dim);

    try {
        H5XX_NO_AUTO_PRINT(H5::Exception);
        dataset.read(data, ctype<T>::hid(), mem_dataspace, dataspace);
    }
    catch (H5::Exception const&) {
        throw std::runtime_error("HDF5 reader: failed to read multidimensional array data");
    }
}

//
// chunks of scalars
//
template <typename T>
typename boost::enable_if<boost::is_fundamental<T>, H5::DataSet>::type
create_dataset(
    H5::CommonFG const& fg
  , std::string const& name
  , hsize_t max_size=H5S_UNLIMITED)
{
    return create_dataset<T, 0>(fg, name, NULL, max_size);
}

template <typename T>
typename boost::enable_if<boost::is_fundamental<T>, H5::DataSet>::type
create_unique_dataset(
    H5::CommonFG const& fg
  , std::string const& name)
{
    return create_unique_dataset<T, 0>(fg, name, NULL);
}

template <typename T>
typename boost::enable_if<boost::is_fundamental<T>, void>::type
write_dataset(H5::DataSet const& dataset, T const& data, hsize_t index=H5S_UNLIMITED)
{
    write_dataset<T, 0>(dataset, &data, index);
}

template <typename T>
typename boost::enable_if<boost::is_fundamental<T>, void>::type
write_unique_dataset(H5::DataSet const& dataset, T const& data)
{
    write_unique_dataset<T, 0>(dataset, &data);
}

template <typename T>
typename boost::enable_if<boost::is_fundamental<T>, hsize_t>::type
read_dataset(H5::DataSet const& dataset, T* data, ssize_t index)
{
    return read_dataset<T, 0>(dataset, data, index);
}

template <typename T>
typename boost::enable_if<boost::is_fundamental<T>, void>::type
read_unique_dataset(H5::DataSet const& dataset, T* data)
{
    return read_unique_dataset<T, 0>(dataset, data);
}

//
// chunks of fixed-size arrays
//
template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_array<T>, boost::is_fundamental<typename T::value_type>
    >, H5::DataSet>::type
create_dataset(
    H5::CommonFG const& fg
  , std::string const& name
  , hsize_t max_size=H5S_UNLIMITED)
{
    typedef typename T::value_type value_type;
    enum { rank = 1 };
    hsize_t shape[1] = { T::static_size };
    return create_dataset<value_type, rank>(fg, name, shape, max_size);
}

template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_array<T>, boost::is_fundamental<typename T::value_type>
    >, H5::DataSet>::type
create_unique_dataset(
    H5::CommonFG const& fg
  , std::string const& name)
{
    typedef typename T::value_type value_type;
    enum { rank = 1 };
    hsize_t shape[1] = { T::static_size };
    return create_unique_dataset<value_type, rank>(fg, name, shape);
}

template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_array<T>, boost::is_fundamental<typename T::value_type>
    >, void>::type
write_dataset(H5::DataSet const& dataset, T const& data, hsize_t index=H5S_UNLIMITED)
{
    typedef typename T::value_type value_type;
    enum { rank = 1 };
    if (!has_extent<T, 1>(dataset))
    {
        throw std::runtime_error("HDF5 writer: dataset has incompatible dataspace");
    }
    write_dataset<value_type, rank>(dataset, &data.front(), index);
}

template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_array<T>, boost::is_fundamental<typename T::value_type>
    >, void>::type
write_unique_dataset(H5::DataSet const& dataset, T const& data)
{
    typedef typename T::value_type value_type;
    enum { rank = 1 };
    if (!has_extent<T>(dataset))
    {
        throw std::runtime_error("HDF5 writer: dataset has incompatible dataspace");
    }
    write_unique_dataset<value_type, rank>(dataset, &data.front());
}

template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_array<T>, boost::is_fundamental<typename T::value_type>
    >, hsize_t>::type
read_dataset(H5::DataSet const& dataset, T* data, ssize_t index)
{
    typedef typename T::value_type value_type;
    enum { rank = 1 };
    return read_dataset<value_type, rank>(dataset, &data->front(), index);
}

template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_array<T>, boost::is_fundamental<typename T::value_type>
    >, void>::type
read_unique_dataset(H5::DataSet const& dataset, T* data)
{
    typedef typename T::value_type value_type;
    enum { rank = 1 };
    read_unique_dataset<value_type, rank>(dataset, &data->front());
}

//
// chunks of multi-arrays of fixed rank
//
template <typename T>
typename boost::enable_if<is_multi_array<T>, H5::DataSet>::type
create_dataset(
    H5::CommonFG const& fg
  , std::string const& name
  , typename T::size_type const* shape
  , hsize_t max_size=H5S_UNLIMITED)
{
    typedef typename T::element value_type;
    enum { rank = T::dimensionality };
    // convert T::size_type to hsize_t
    boost::array<hsize_t, rank> shape_;
    std::copy(shape, shape + rank, shape_.begin());
    return create_dataset<value_type, rank>(fg, name, &shape_.front(), max_size);
}

template <typename T>
typename boost::enable_if<is_multi_array<T>, H5::DataSet>::type
create_unique_dataset(
    H5::CommonFG const& fg
  , std::string const& name
  , typename T::size_type const* shape)
{
    typedef typename T::element value_type;
    enum { rank = T::dimensionality };
    // convert T::size_type to hsize_t
    boost::array<hsize_t, rank> shape_;
    std::copy(shape, shape + rank, shape_.begin());
    return create_unique_dataset<value_type, rank>(fg, name, &shape_.front());
}

template <typename T>
typename boost::enable_if<is_multi_array<T>, void>::type
write_dataset(H5::DataSet const& dataset, T const& data, hsize_t index=H5S_UNLIMITED)
{
    typedef typename T::element value_type;
    enum { rank = T::dimensionality };
    if (!has_extent<T, 1>(dataset, data.shape()))
    {
        throw std::runtime_error("HDF5 writer: dataset has incompatible dataspace");
    }
    write_dataset<value_type, rank>(dataset, data.origin(), index);
}

template <typename T>
typename boost::enable_if<is_multi_array<T>, void>::type
write_unique_dataset(H5::DataSet const& dataset, T const& data)
{
    typedef typename T::element value_type;
    enum { rank = T::dimensionality };
    if (!has_extent<T>(dataset, data.shape()))
    {
        throw std::runtime_error("HDF5 writer: dataset has incompatible dataspace");
    }
    write_unique_dataset<value_type, rank>(dataset, data.origin());
}

/** read chunk of multi_array data, resize/reshape result array if necessary */
template <typename T>
typename boost::enable_if<is_multi_array<T>, hsize_t>::type
read_dataset(H5::DataSet const& dataset, T* data, ssize_t index)
{
    typedef typename T::element value_type;
    enum { rank = T::dimensionality };

    // determine extent of data space
    H5::DataSpace dataspace(dataset.getSpace());
    if (!has_rank<rank+1>(dataspace)) {
        throw std::runtime_error("HDF5 reader: dataset has incompatible dataspace");
    }
    boost::array<hsize_t, rank+1> dim;
    dataspace.getSimpleExtentDims(&dim.front());

    // resize result array if necessary, may allocate new memory
    if (!std::equal(dim.begin() + 1, dim.end(), data->shape())) {
        boost::array<size_t, rank> shape;
        std::copy(dim.begin() + 1, dim.end(), shape.begin());
        data->resize(shape);
    }

    return read_dataset<value_type, rank>(dataset, data->origin(), index);
}

template <typename T>
typename boost::enable_if<is_multi_array<T>, void>::type
read_unique_dataset(H5::DataSet const& dataset, T* data)
{
    typedef typename T::element value_type;
    enum { rank = T::dimensionality };

    // determine extent of data space
    H5::DataSpace dataspace(dataset.getSpace());
    if (!has_rank<rank>(dataspace)) {
        throw std::runtime_error("HDF5 reader: dataset has incompatible dataspace");
    }
    boost::array<hsize_t, rank> dim;
    dataspace.getSimpleExtentDims(&dim.front());

    // resize result array if necessary, may allocate new memory
    if (!std::equal(dim.begin(), dim.end(), data->shape())) {
        data->resize(dim);
    }

    return read_unique_dataset<value_type, rank>(dataset, data->origin());
}

//
// chunks of vector containers holding scalars
//
// pass length of vector as third parameter
template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_vector<T>, boost::is_fundamental<typename T::value_type>
    >, H5::DataSet>::type
create_dataset(
    H5::CommonFG const& fg
  , std::string const& name
  , typename T::size_type size
  , hsize_t max_size=H5S_UNLIMITED)
{
    typedef typename T::value_type value_type;
    hsize_t shape[1] = { size };
    return create_dataset<value_type, 1>(fg, name, shape, max_size);
}

template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_vector<T>, boost::is_fundamental<typename T::value_type>
    >, H5::DataSet>::type
create_unique_dataset(
    H5::CommonFG const& fg
  , std::string const& name
  , typename T::size_type size)
{
    typedef typename T::value_type value_type;
    hsize_t shape[1] = { size };
    return create_unique_dataset<value_type, 1>(fg, name, shape);
}

template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_vector<T>, boost::is_fundamental<typename T::value_type>
    >, void>::type
write_dataset(H5::DataSet const& dataset, T const& data, hsize_t index=H5S_UNLIMITED)
{
    typedef typename T::value_type value_type;

    // assert data.size() corresponds to dataspace extents
    if (has_rank<2>(dataset)) {
        hsize_t dim[2];
        dataset.getSpace().getSimpleExtentDims(dim);
        if (data.size() != dim[1]) {
            throw std::runtime_error("HDF5 writer: dataset has incompatible dataspace");
        }
    }

    write_dataset<value_type, 1>(dataset, &*data.begin(), index);
}

template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_vector<T>, boost::is_fundamental<typename T::value_type>
    >, void>::type
write_unique_dataset(H5::DataSet const& dataset, T const& data)
{
    typedef typename T::value_type value_type;

    // assert data.size() corresponds to dataspace extents
    if (has_rank<1>(dataset)) {
        hsize_t dim;
        dataset.getSpace().getSimpleExtentDims(&dim);
        if (data.size() != dim) {
            throw std::runtime_error("HDF5 writer: dataset has incompatible dataspace");
        }
    }

    write_unique_dataset<value_type, 1>(dataset, &*data.begin());
}

/** read chunk of vector container with scalar data, resize/reshape result array if necessary */
template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_vector<T>, boost::is_fundamental<typename T::value_type>
    >, hsize_t>::type
read_dataset(H5::DataSet const& dataset, T* data, ssize_t index)
{
    typedef typename T::value_type value_type;

    // determine extent of data space and resize result vector (if necessary)
    H5::DataSpace dataspace(dataset.getSpace());
    if (!has_rank<2>(dataspace)) {
        throw std::runtime_error("HDF5 reader: dataset has incompatible dataspace");
    }
    hsize_t dim[2];
    dataspace.getSimpleExtentDims(dim);
    data->resize(dim[1]);

    return read_dataset<value_type, 1>(dataset, &*data->begin(), index);
}

template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_vector<T>, boost::is_fundamental<typename T::value_type>
    >, void>::type
read_unique_dataset(H5::DataSet const& dataset, T* data)
{
    typedef typename T::value_type value_type;

    // determine extent of data space and resize result vector (if necessary)
    H5::DataSpace dataspace(dataset.getSpace());
    if (!has_rank<1>(dataspace)) {
        throw std::runtime_error("HDF5 reader: dataset has incompatible dataspace");
    }
    hsize_t dim;
    dataspace.getSimpleExtentDims(&dim);
    data->resize(dim);

    read_unique_dataset<value_type, 1>(dataset, &*data->begin());
}

//
// chunks of vector containers holding fixed-size arrays
//
// pass length of vector as third parameter
template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_vector<T>, is_array<typename T::value_type>
    >, H5::DataSet>::type
create_dataset(
    H5::CommonFG const& fg
  , std::string const& name
  , typename T::size_type size
  , hsize_t max_size=H5S_UNLIMITED)
{
    typedef typename T::value_type array_type;
    typedef typename array_type::value_type value_type;
    hsize_t shape[2] = { size, array_type::static_size };
    return create_dataset<value_type, 2>(fg, name, shape, max_size);
}

template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_vector<T>, is_array<typename T::value_type>
    >, H5::DataSet>::type
create_unique_dataset(
    H5::CommonFG const& fg
  , std::string const& name
  , typename T::size_type size)
{
    typedef typename T::value_type array_type;
    typedef typename array_type::value_type value_type;
    hsize_t shape[2] = { size, array_type::static_size };
    return create_unique_dataset<value_type, 2>(fg, name, shape);
}

template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_vector<T>, is_array<typename T::value_type>
    >, void>::type
write_dataset(H5::DataSet const& dataset, T const& data, hsize_t index=H5S_UNLIMITED)
{
    typedef typename T::value_type array_type;
    typedef typename array_type::value_type value_type;

    // assert data.size() corresponds to dataspace extents
    if (has_rank<3>(dataset)) {
        hsize_t dim[3];
        dataset.getSpace().getSimpleExtentDims(dim);
        if (data.size() != dim[1]) {
            throw std::runtime_error("HDF5 writer: dataset has incompatible dataspace");
        }
    }

    // raw data are laid out contiguously
    write_dataset<value_type, 2>(dataset, &data.front().front(), index);
}

template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_vector<T>, is_array<typename T::value_type>
    >, void>::type
write_unique_dataset(H5::DataSet const& dataset, T const& data)
{
    typedef typename T::value_type array_type;
    typedef typename array_type::value_type value_type;

    // assert data.size() corresponds to dataspace extents
    if (has_rank<2>(dataset)) {
        hsize_t dim[2];
        dataset.getSpace().getSimpleExtentDims(dim);
        if (data.size() != dim[0]) {
            throw std::runtime_error("HDF5 writer: dataset has incompatible dataspace");
        }
    }

    // raw data are laid out contiguously
    write_unique_dataset<value_type, 2>(dataset, &data.front().front());
}

/** read chunk of vector container with array data, resize/reshape result array if necessary */
template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_vector<T>, is_array<typename T::value_type>
    >, hsize_t>::type
read_dataset(H5::DataSet const& dataset, T* data, ssize_t index)
{
    typedef typename T::value_type array_type;
    typedef typename array_type::value_type value_type;

    // determine extent of data space and resize result vector (if necessary)
    H5::DataSpace dataspace(dataset.getSpace());
    if (!has_rank<3>(dataspace)) {
        throw std::runtime_error("HDF5 reader: dataset has incompatible dataspace");
    }
    hsize_t dim[3];
    dataspace.getSimpleExtentDims(dim);
    data->resize(dim[1]);

    // raw data are laid out contiguously
    return read_dataset<value_type, 2>(dataset, &data->front().front(), index);
}

template <typename T>
typename boost::enable_if<boost::mpl::and_<
        is_vector<T>, is_array<typename T::value_type>
    >, void>::type
read_unique_dataset(H5::DataSet const& dataset, T* data)
{
    typedef typename T::value_type array_type;
    typedef typename array_type::value_type value_type;

    // determine extent of data space and resize result vector (if necessary)
    H5::DataSpace dataspace(dataset.getSpace());
    if (!has_rank<2>(dataspace)) {
        throw std::runtime_error("HDF5 reader: dataset has incompatible dataspace");
    }
    hsize_t dim[2];
    dataspace.getSimpleExtentDims(dim);
    data->resize(dim[0]);

    // raw data are laid out contiguously
    read_unique_dataset<value_type, 2>(dataset, &data->front().front());
}

/**
 * Helper function to create a unique dataset on the fly and write to it.
 *
 * Note that this function overloads the generic write_unique_dataset(H5::DataSet, T const&).
 */
template <typename T>
void write_unique_dataset(H5::CommonFG const& fg, std::string const& name, T const& data)
{
    H5::DataSet dataset = create_unique_dataset<T>(fg, name);
    write_unique_dataset(dataset, data);
}

/**
 * Helper function to open a unique dataset on the fly and read from it.
 *
 * Note that this function overloads the generic write_unique_dataset(H5::DataSet, T const&).
 */
template <typename T>
void read_unique_dataset(H5::CommonFG const& fg, std::string const& name, T* data)
{
    H5E_BEGIN_TRY {
        // open dataset in file or group
        H5::IdComponent const& loc(dynamic_cast<H5::IdComponent const&>(fg));
        hid_t hid = H5Dopen(loc.getId(), name.c_str(), H5P_DEFAULT);
        if (hid > 0) {
            read_unique_dataset(hid, data);
            H5Dclose(hid);
        }
        else {
            throw error("attempt to read non-existent dataset \"" + name + "\"");
        }
    } H5E_END_TRY
}

} // namespace h5xx

#endif /* ! H5XX_DATASET_HPP */
