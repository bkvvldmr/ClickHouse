#pragma once
#include <Functions/IFunction.h>
#include <Functions/FunctionHelpers.h>
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypesNumber.h>
#include <Columns/ColumnArray.h>
#include <Columns/ColumnNullable.h>
#include <Columns/ColumnsNumber.h>
#include <Columns/ColumnString.h>
#include <Interpreters/Context_fwd.h>
#include <Common/HashTable/ClearableHashMap.h>
#include <Common/ColumnsHashing.h>


namespace DB
{

namespace ErrorCodes
{
    extern const int TOO_FEW_ARGUMENTS_FOR_FUNCTION;
    extern const int ILLEGAL_COLUMN;
    extern const int ILLEGAL_TYPE_OF_ARGUMENT;
    extern const int SIZES_OF_ARRAYS_DONT_MATCH;
}

class FunctionArrayEnumerateUniq;
class FunctionArrayEnumerateDense;

template <typename Derived>
class FunctionArrayEnumerateExtended : public IFunction
{
public:
    static FunctionPtr create(ContextPtr) { return std::make_shared<Derived>(); }

    String getName() const override { return Derived::name; }

    bool isVariadic() const override { return true; }
    size_t getNumberOfArguments() const override { return 0; }
    bool useDefaultImplementationForConstants() const override { return true; }
    bool isSuitableForShortCircuitArgumentsExecution(const DataTypesWithConstInfo & /*arguments*/) const override { return true; }

    DataTypePtr getReturnTypeImpl(const DataTypes & arguments) const override
    {
        if (arguments.empty())
            throw Exception(ErrorCodes::TOO_FEW_ARGUMENTS_FOR_FUNCTION,
                "Number of arguments for function {} doesn't match: passed {}, should be at least 1.",
                getName(), arguments.size());

        for (size_t i = 0; i < arguments.size(); ++i)
        {
            const DataTypeArray * array_type = checkAndGetDataType<DataTypeArray>(arguments[i].get());
            if (!array_type)
                throw Exception(ErrorCodes::ILLEGAL_TYPE_OF_ARGUMENT,
                                "All arguments for function {} must be arrays but argument {} has type {}.",
                                getName(), i + 1, arguments[i]->getName());
        }

        return std::make_shared<DataTypeArray>(std::make_shared<DataTypeUInt32>());
    }

    ColumnPtr executeImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr &, size_t input_rows_count) const override;

private:
    /// Initially allocate a piece of memory for 64 elements. NOTE: This is just a guess.
    static constexpr size_t INITIAL_SIZE_DEGREE = 6;

    template <typename T>
    struct MethodOneNumber
    {
        using Set = ClearableHashMapWithStackMemory<T, UInt32, DefaultHash<T>,
            INITIAL_SIZE_DEGREE>;

        using Method = ColumnsHashing::HashMethodOneNumber<typename Set::value_type, UInt32, T, false>;
    };

    struct MethodString
    {
        using Set = ClearableHashMapWithStackMemory<StringRef, UInt32, StringRefHash,
            INITIAL_SIZE_DEGREE>;

        using Method = ColumnsHashing::HashMethodString<typename Set::value_type, UInt32, false, false>;
    };

    struct MethodFixedString
    {
        using Set = ClearableHashMapWithStackMemory<StringRef, UInt32, StringRefHash,
            INITIAL_SIZE_DEGREE>;

        using Method = ColumnsHashing::HashMethodFixedString<typename Set::value_type, UInt32, false, false>;
    };

    struct MethodFixed
    {
        using Set = ClearableHashMapWithStackMemory<UInt128, UInt32, UInt128HashCRC32,
            INITIAL_SIZE_DEGREE>;

        using Method = ColumnsHashing::HashMethodKeysFixed<typename Set::value_type, UInt128, UInt32, false, false, false>;
    };

    struct MethodHashed
    {
        using Set = ClearableHashMapWithStackMemory<UInt128, UInt32, UInt128TrivialHash,
            INITIAL_SIZE_DEGREE>;

        using Method = ColumnsHashing::HashMethodHashed<typename Set::value_type, UInt32, false>;
    };

    template <typename Method>
    void executeMethod(const ColumnArray::Offsets & offsets, const ColumnRawPtrs & columns, const Sizes & key_sizes,
                       const NullMap * null_map, ColumnUInt32::Container & res_values) const;

    template <typename Method, bool has_null_map>
    void executeMethodImpl(const ColumnArray::Offsets & offsets, const ColumnRawPtrs & columns, const Sizes & key_sizes,
                           const NullMap * null_map, ColumnUInt32::Container & res_values) const;

    template <typename T>
    bool executeNumber(const ColumnArray::Offsets & offsets, const IColumn & data, const NullMap * null_map, ColumnUInt32::Container & res_values) const;
    bool executeString(const ColumnArray::Offsets & offsets, const IColumn & data, const NullMap * null_map, ColumnUInt32::Container & res_values) const;
    bool executeFixedString(const ColumnArray::Offsets & offsets, const IColumn & data, const NullMap * null_map, ColumnUInt32::Container & res_values) const;
    bool execute128bit(const ColumnArray::Offsets & offsets, const ColumnRawPtrs & columns, ColumnUInt32::Container & res_values) const;
    void executeHashed(const ColumnArray::Offsets & offsets, const ColumnRawPtrs & columns, ColumnUInt32::Container & res_values) const;
};


template <typename Derived>
ColumnPtr FunctionArrayEnumerateExtended<Derived>::executeImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr &, size_t /*input_rows_count*/) const
{
    const ColumnArray::Offsets * offsets = nullptr;
    size_t num_arguments = arguments.size();
    ColumnRawPtrs data_columns(num_arguments);

    Columns array_holders;
    ColumnPtr offsets_column;
    for (size_t i = 0; i < num_arguments; ++i)
    {
        const ColumnPtr & array_ptr = arguments[i].column;
        const ColumnArray * array = checkAndGetColumn<ColumnArray>(array_ptr.get());
        if (!array)
        {
            const ColumnConst * const_array = checkAndGetColumnConst<ColumnArray>(
                arguments[i].column.get());
            if (!const_array)
                throw Exception(ErrorCodes::ILLEGAL_COLUMN, "Illegal column {} of {}-th argument of function {}",
                    arguments[i].column->getName(), i + 1, getName());
            array_holders.emplace_back(const_array->convertToFullColumn());
            array = checkAndGetColumn<ColumnArray>(array_holders.back().get());
        }

        const ColumnArray::Offsets & offsets_i = array->getOffsets();
        if (i == 0)
        {
            offsets = &offsets_i;
            offsets_column = array->getOffsetsPtr();
        }
        else if (offsets_i != *offsets)
            throw Exception(ErrorCodes::SIZES_OF_ARRAYS_DONT_MATCH, "Lengths of all arrays passed to {} must be equal.",
                getName());

        const auto * array_data = &array->getData();
        data_columns[i] = array_data;
    }

    const NullMap * null_map = nullptr;

    for (size_t i = 0; i < num_arguments; ++i)
    {
        if (const auto * nullable_col = checkAndGetColumn<ColumnNullable>(data_columns[i]))
        {
            if (num_arguments == 1)
                data_columns[i] = &nullable_col->getNestedColumn();

            null_map = &nullable_col->getNullMapData();
            break;
        }
    }

    auto res_nested = ColumnUInt32::create();

    ColumnUInt32::Container & res_values = res_nested->getData();
    if (!offsets->empty())
        res_values.resize(offsets->back());

    if (num_arguments == 1)
    {
        if (!(executeNumber<UInt8>(*offsets, *data_columns[0], null_map, res_values)
            || executeNumber<UInt16>(*offsets, *data_columns[0], null_map, res_values)
            || executeNumber<UInt32>(*offsets, *data_columns[0], null_map, res_values)
            || executeNumber<UInt64>(*offsets, *data_columns[0], null_map, res_values)
            || executeNumber<Int8>(*offsets, *data_columns[0], null_map, res_values)
            || executeNumber<Int16>(*offsets, *data_columns[0], null_map, res_values)
            || executeNumber<Int32>(*offsets, *data_columns[0], null_map, res_values)
            || executeNumber<Int64>(*offsets, *data_columns[0], null_map, res_values)
            || executeNumber<Float32>(*offsets, *data_columns[0], null_map, res_values)
            || executeNumber<Float64>(*offsets, *data_columns[0], null_map, res_values)
            || executeString(*offsets, *data_columns[0], null_map, res_values)
            || executeFixedString(*offsets, *data_columns[0], null_map, res_values)))
            executeHashed(*offsets, data_columns, res_values);
    }
    else
    {
        if (!execute128bit(*offsets, data_columns, res_values))
            executeHashed(*offsets, data_columns, res_values);
    }

    return ColumnArray::create(std::move(res_nested), offsets_column);
}

template <typename Derived>
template <typename Method, bool has_null_map>
void FunctionArrayEnumerateExtended<Derived>::executeMethodImpl(
        const ColumnArray::Offsets & offsets,
        const ColumnRawPtrs & columns,
        const Sizes & key_sizes,
        [[maybe_unused]] const NullMap * null_map,
        ColumnUInt32::Container & res_values) const
{
    typename Method::Set indices;
    typename Method::Method method(columns, key_sizes, nullptr);
    Arena pool; /// Won't use it;

    ColumnArray::Offset prev_off = 0;

    if constexpr (std::is_same_v<Derived, FunctionArrayEnumerateUniq>)
    {
        // Unique
        for (size_t off : offsets)
        {
            indices.clear();
            UInt32 null_count = 0;
            for (size_t j = prev_off; j < off; ++j)
            {
                if constexpr (has_null_map)
                {
                    if ((*null_map)[j])
                    {
                        res_values[j] = ++null_count;
                        continue;
                    }
                }

                auto emplace_result = method.emplaceKey(indices, j, pool);
                auto idx = emplace_result.getMapped() + 1;
                emplace_result.setMapped(idx);

                res_values[j] = idx;
            }
            prev_off = off;
        }
    }
    else
    {
        // Dense
        for (size_t off : offsets)
        {
            indices.clear();
            UInt32 rank = 0;
            [[maybe_unused]] UInt32 null_index = 0;
            for (size_t j = prev_off; j < off; ++j)
            {
                if constexpr (has_null_map)
                {
                    if ((*null_map)[j])
                    {
                        if (!null_index)
                            null_index = ++rank;

                        res_values[j] = null_index;
                        continue;
                    }
                }

                auto emplace_result = method.emplaceKey(indices, j, pool);
                auto idx = emplace_result.getMapped();

                if (!idx)
                {
                    idx = ++rank;
                    emplace_result.setMapped(idx);
                }

                res_values[j] = idx;
            }
            prev_off = off;
        }
    }
}

template <typename Derived>
template <typename Method>
void FunctionArrayEnumerateExtended<Derived>::executeMethod(
    const ColumnArray::Offsets & offsets,
    const ColumnRawPtrs & columns,
    const Sizes & key_sizes,
    const NullMap * null_map,
    ColumnUInt32::Container & res_values) const
{
    if (null_map)
        executeMethodImpl<Method, true>(offsets, columns, key_sizes, null_map, res_values);
    else
        executeMethodImpl<Method, false>(offsets, columns, key_sizes, null_map, res_values);

}

template <typename Derived>
template <typename T>
bool FunctionArrayEnumerateExtended<Derived>::executeNumber(
    const ColumnArray::Offsets & offsets, const IColumn & data, const NullMap * null_map, ColumnUInt32::Container & res_values) const
{
    const auto * nested = checkAndGetColumn<ColumnVector<T>>(&data);
    if (!nested)
        return false;

    executeMethod<MethodOneNumber<T>>(offsets, {nested}, {}, null_map, res_values);
    return true;
}

template <typename Derived>
bool FunctionArrayEnumerateExtended<Derived>::executeString(
    const ColumnArray::Offsets & offsets, const IColumn & data, const NullMap * null_map, ColumnUInt32::Container & res_values) const
{
    const auto * nested = checkAndGetColumn<ColumnString>(&data);
    if (nested)
        executeMethod<MethodString>(offsets, {nested}, {}, null_map, res_values);

    return nested;
}

template <typename Derived>
bool FunctionArrayEnumerateExtended<Derived>::executeFixedString(
        const ColumnArray::Offsets & offsets, const IColumn & data, const NullMap * null_map, ColumnUInt32::Container & res_values) const
{
    const auto * nested = checkAndGetColumn<ColumnString>(&data);
    if (nested)
        executeMethod<MethodFixedString>(offsets, {nested}, {}, null_map, res_values);

    return nested;
}

template <typename Derived>
bool FunctionArrayEnumerateExtended<Derived>::execute128bit(
    const ColumnArray::Offsets & offsets,
    const ColumnRawPtrs & columns,
    ColumnUInt32::Container & res_values) const
{
    size_t count = columns.size();
    size_t keys_bytes = 0;
    Sizes key_sizes(count);

    for (size_t j = 0; j < count; ++j)
    {
        if (!columns[j]->isFixedAndContiguous())
            return false;
        key_sizes[j] = columns[j]->sizeOfValueIfFixed();
        keys_bytes += key_sizes[j];
    }

    if (keys_bytes > 16)
        return false;

    executeMethod<MethodFixed>(offsets, columns, key_sizes, nullptr, res_values);
    return true;
}

template <typename Derived>
void FunctionArrayEnumerateExtended<Derived>::executeHashed(
    const ColumnArray::Offsets & offsets,
    const ColumnRawPtrs & columns,
    ColumnUInt32::Container & res_values) const
{
    executeMethod<MethodHashed>(offsets, columns, {}, nullptr, res_values);
}

}
