#include "test_legacy_base.hpp"
#include <cfloat>
#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

/*!
 * \brief Tests of the generated migration routines, in the conversions configuration: MEEM_MigrateBlock() is
 *        called directly on the images of the legacy models, with sources that hold values at the edges, and the converted caches are
 *        checked. Every current block has a routine (test/meem_config/eeprom_datamodel_conversions*.json): the blocks From_<type> cover
 *        every pair of data types, the others bitfields, multiplicities, reordered parameters, profiles and wear-leveling blocks. The two
 *        legacy models share every routine.
 */
class ConversionsTest : public LegacyTestBase
{
  public:
    void SetUp() override
    {
        LegacyTestBase::SetUp(); // Important: Call base class SetUp() first
        MEEM_DeInit();
        LoadLegacyImage(0);
    }

    /// @brief Restores the block's defaults, as the core does, and migrates it
    static bool Migrate(uint8_t block_id, uint8_t k = 0, uint8_t profile = 0)
    {
        MEEM_RestoreDefaults(block_id);
        return MEEM_MigrateBlock(k, block_id, profile);
    }

    template <typename P> static std::vector<uint8_t> BytesOf(const P& params)
    {
        const auto bytes = reinterpret_cast<const uint8_t*>(&params);
        return std::vector<uint8_t>(bytes, bytes + sizeof(params));
    }

    /// @brief Stores the value into each of the ten parameters of a block From_<type> of legacy model k
    template <typename S> void StoreTenTimes(uint8_t block_id, S value, uint8_t k = 0)
    {
        std::array<S, 10> params;
        params.fill(value);
        StoreInstance(RoutineSourceOf(k, block_id), 0, BytesOf(params));
    }

    /// @brief Migrates every block from legacy model 0 through MEEM_Init(), then the rewrites and the header write, which commits the migration
    void MigrateThroughInit()
    {
        MEEM_Init();
        ProcessMeemUntilIdle();
        EXPECT_EQ(GetHeaderFromEeprom(), CurrentHeader()) << "Committed";

        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            EXPECT_TRUE(MEEM_GetBlockStatus(block_id).migrated) << "Block ID " << int(block_id);
        }
    }

    /// @brief Restarts the device after the commit: MEEM_Init() initializes every block as in v1 and finds every one valid at its new place
    void Restart()
    {
        MEEM_DeInit();
        MEEM_Init();

        EXPECT_FALSE(MEEM_IsBusy()) << "Nothing to write";
        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            EXPECT_FALSE(MEEM_GetBlockStatus(block_id).recovered) << "Block ID " << int(block_id);
        }
    }
};

/// @brief A conversion, computed independently of the generated code
template <typename T, typename S> static T Expected(S value)
{
    using Limits = std::numeric_limits<T>;

    if constexpr (std::is_floating_point_v<S> && std::is_floating_point_v<T>)
    {
        // float64 -> float32: the nearest value, clamped to +-max, NaN stays NaN. float32 -> float64 is exact.
        if ((sizeof(T) < sizeof(S)) && !std::isnan(value))
        {
            return static_cast<T>(std::clamp<S>(value, -Limits::max(), Limits::max()));
        }
        return static_cast<T>(value);
    }
    else if constexpr (std::is_floating_point_v<S>)
    {
        // Float -> integer: rounded half away from zero, then clamped. NaN gives 0.
        if (std::isnan(value))
        {
            return 0;
        }
        const double rounded = std::round(static_cast<double>(value));
        const double limit   = std::ldexp(1.0, Limits::digits); // The maximum + 1, exact

        if (rounded >= limit)
        {
            return Limits::max();
        }
        if (rounded < (std::is_signed_v<T> ? -limit : 0.0))
        {
            return Limits::min();
        }
        return static_cast<T>(rounded);
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        // Integer -> float: the nearest value. Every integer is below FLT_MAX.
        return static_cast<T>(value);
    }
    else
    {
        // Integer -> integer: clamped
        if (std::cmp_less(value, Limits::min()))
        {
            return Limits::min();
        }
        if (std::cmp_greater(value, Limits::max()))
        {
            return Limits::max();
        }
        return static_cast<T>(value);
    }
}

template <typename T> static void ExpectConverted(T actual, T expected, const char* target)
{
    if constexpr (std::is_floating_point_v<T>)
    {
        if (std::isnan(expected))
        {
            EXPECT_TRUE(std::isnan(actual)) << target;
            return;
        }
    }
    EXPECT_EQ(actual, expected) << target;
}

/// @brief The values of a source type at the edges of the conversions: the limits of every type, x.5 rounding, NaN and infinities
template <typename S> static std::vector<S> EdgeValues()
{
    std::vector<S> values;

    if constexpr (std::is_integral_v<S>)
    {
        const std::vector<int64_t>  negative{INT64_MIN, INT64_MIN + 1, -(int64_t{1} << 53) - 1, INT32_MIN - int64_t{1}, INT32_MIN, -32769, -32768, -129, -128,
                                             -1};
        const std::vector<uint64_t> positive{0,
                                             1,
                                             127,
                                             128,
                                             255,
                                             256,
                                             32767,
                                             32768,
                                             65535,
                                             65536,
                                             (1u << 24) + 1u,
                                             INT32_MAX,
                                             0x80000000u,
                                             UINT32_MAX,
                                             uint64_t{1} << 32,
                                             (uint64_t{1} << 53) + 1u,
                                             INT64_MAX,
                                             uint64_t{1} << 63,
                                             UINT64_MAX};

        for (const auto value : negative)
        {
            if (std::in_range<S>(value))
            {
                values.push_back(static_cast<S>(value));
            }
        }
        for (const auto value : positive)
        {
            if (std::in_range<S>(value))
            {
                values.push_back(static_cast<S>(value));
            }
        }
    }
    else
    {
        const double              inf = std::numeric_limits<double>::infinity();
        const std::vector<double> candidates{std::numeric_limits<double>::quiet_NaN(),
                                             -std::numeric_limits<double>::quiet_NaN(),
                                             inf,
                                             -inf,
                                             0.0,
                                             -0.0,
                                             0.49999999999999994,
                                             0.5,
                                             -0.5,
                                             -0.4,
                                             1.5,
                                             2.5,
                                             -2.5,
                                             126.5,
                                             127.4,
                                             127.5,
                                             -128.4,
                                             -128.5,
                                             254.5,
                                             255.4,
                                             255.5,
                                             32767.5,
                                             -32768.5,
                                             65534.5,
                                             65535.5,
                                             16777217.0,
                                             2147483520.0,
                                             2147483647.5,
                                             2147483648.0,
                                             -2147483648.5,
                                             -2147483904.0,
                                             4294967294.5,
                                             4294967295.5,
                                             4294967296.0,
                                             9007199254740993.0,
                                             9223372036854774784.0,
                                             9223372036854775808.0,
                                             -9223372036854775808.0,
                                             -9223372036854777856.0,
                                             18446744073709549568.0,
                                             18446744073709551616.0,
                                             1e19,
                                             1e-45,
                                             5e-324,
                                             3.0e38,
                                             -3.0e38,
                                             FLT_MAX,
                                             -FLT_MAX,
                                             3.4028235677973370e38,
                                             3.5e38,
                                             -3.5e38,
                                             1e300,
                                             -1e300,
                                             DBL_MAX,
                                             -DBL_MAX};

        for (const auto value : candidates)
        {
            // A float holds only the infinities beyond its range
            if (std::is_same_v<S, double> || std::isnan(value) || std::isinf(value) || (std::fabs(value) <= FLT_MAX))
            {
                values.push_back(static_cast<S>(value));
            }
        }
    }
    return values;
}

/******************************************************************************/
/*    Every pair of data types                                                */
/******************************************************************************/
#define EXPECT_CONVERTED(cache, target, value)                                                                                                                 \
    {                                                                                                                                                          \
        using T = decltype(cache.target);                                                                                                                      \
        ExpectConverted<T>(cache.target, Expected<T>(value), #target);                                                                                         \
    }

#define TEST_CONVERSIONS_FROM(source_type, S)                                                                                                                  \
    TEST_F(ConversionsTest, From_##source_type##_IntoEveryType)                                                                                                \
    {                                                                                                                                                          \
        for (const S value : EdgeValues<S>())                                                                                                                  \
        {                                                                                                                                                      \
            SCOPED_TRACE(#source_type " " + testing::PrintToString(+value));                                                                                   \
            StoreTenTimes(MEEM_BLOCK_From_##source_type##_ID, value);                                                                                          \
            ASSERT_TRUE(Migrate(MEEM_BLOCK_From_##source_type##_ID));                                                                                          \
                                                                                                                                                               \
            EXPECT_CONVERTED(MEEM_cache_From_##source_type, to_uint8, value);                                                                                  \
            EXPECT_CONVERTED(MEEM_cache_From_##source_type, to_int8, value);                                                                                   \
            EXPECT_CONVERTED(MEEM_cache_From_##source_type, to_uint16, value);                                                                                 \
            EXPECT_CONVERTED(MEEM_cache_From_##source_type, to_int16, value);                                                                                  \
            EXPECT_CONVERTED(MEEM_cache_From_##source_type, to_uint32, value);                                                                                 \
            EXPECT_CONVERTED(MEEM_cache_From_##source_type, to_int32, value);                                                                                  \
            EXPECT_CONVERTED(MEEM_cache_From_##source_type, to_uint64, value);                                                                                 \
            EXPECT_CONVERTED(MEEM_cache_From_##source_type, to_int64, value);                                                                                  \
            EXPECT_CONVERTED(MEEM_cache_From_##source_type, to_float32, value);                                                                                \
            EXPECT_CONVERTED(MEEM_cache_From_##source_type, to_float64, value);                                                                                \
        }                                                                                                                                                      \
    }

TEST_CONVERSIONS_FROM(uint8, uint8_t)
TEST_CONVERSIONS_FROM(int8, int8_t)
TEST_CONVERSIONS_FROM(uint16, uint16_t)
TEST_CONVERSIONS_FROM(int16, int16_t)
TEST_CONVERSIONS_FROM(uint32, uint32_t)
TEST_CONVERSIONS_FROM(int32, int32_t)
TEST_CONVERSIONS_FROM(uint64, uint64_t)
TEST_CONVERSIONS_FROM(int64, int64_t)
TEST_CONVERSIONS_FROM(float32, float)
TEST_CONVERSIONS_FROM(float64, double)

TEST_F(ConversionsTest, SpotChecksOfTheReference)
{
    // The reference above against values worked out by hand, so that a mistake in it doesn't hide one in the routines
    EXPECT_EQ(Expected<uint8_t>(-1), 0u);
    EXPECT_EQ(Expected<int8_t>(uint64_t{UINT64_MAX}), 127);
    EXPECT_EQ(Expected<uint32_t>(int64_t{INT64_MIN}), 0u);
    EXPECT_EQ(Expected<int16_t>(2.5), 3);
    EXPECT_EQ(Expected<int16_t>(-2.5f), -3);
    EXPECT_EQ(Expected<uint8_t>(254.5), 255u);
    EXPECT_EQ(Expected<uint8_t>(255.5), 255u);
    EXPECT_EQ(Expected<uint8_t>(-0.5f), 0u);
    EXPECT_EQ(Expected<int32_t>(2147483648.0f), INT32_MAX);
    EXPECT_EQ(Expected<uint64_t>(18446744073709551616.0), UINT64_MAX);
    EXPECT_EQ(Expected<int64_t>(-9223372036854777856.0), INT64_MIN);
    EXPECT_EQ(Expected<uint16_t>(std::numeric_limits<double>::quiet_NaN()), 0u);
    EXPECT_EQ(Expected<float>(1e300), FLT_MAX);
    EXPECT_EQ(Expected<float>(-std::numeric_limits<double>::infinity()), -FLT_MAX);
    EXPECT_EQ(Expected<double>(std::numeric_limits<float>::infinity()), std::numeric_limits<double>::infinity());
}

/******************************************************************************/
/*    Bitfields                                                               */
/******************************************************************************/
#pragma pack(push, 1)
/// @brief The parameters of Bitfields in the legacy model, as its routine reads them
struct LegacyBitfields {
    union {
        uint8_t all;
        struct {
            uint8_t a : 3;
            uint8_t b : 3;
        };
    } narrowed;
    union {
        int8_t all;
        struct {
            int8_t s : 4;
        };
    } signed_narrowed;
    union {
        uint8_t all;
        struct {
            uint8_t u : 4;
        };
    } to_signed;
    union {
        int8_t all;
        struct {
            int8_t n : 4;
        };
    } to_unsigned;
    union {
        uint8_t all;
        struct {
            uint8_t kept    : 4;
            uint8_t dropped : 4;
        };
    } renamed;
    union {
        uint8_t all;
        struct {
            uint8_t x : 4;
            uint8_t y : 4;
        };
    } wider_container;
    uint16_t gains;
    union {
        uint16_t all;
        struct {
            uint16_t lo : 8;
            uint16_t hi : 8;
        };
    } loses;
    union {
        uint8_t all;
        struct {
            uint8_t a : 3;
            uint8_t b : 3;
        };
    } array[3];
    union {
        uint64_t all;
        struct {
            uint64_t big   : 48;
            uint64_t small : 3;
        };
    } wide;
    union {
        int64_t all;
        struct {
            int64_t v : 48;
        };
    } signed_wide;
};
#pragma pack(pop)

static_assert(sizeof(LegacyBitfields) == 29);

/// @brief A cached value by value, also a bitfield or a packed member, which a reference can't bind to
#define VALUE(member) static_cast<int64_t>(member)

TEST_F(ConversionsTest, Bitfields_ClampedToTheirNewRanges)
{
    LegacyBitfields old{};
    old.narrowed.a        = 4; // Just above the new range
    old.narrowed.b        = 6;
    old.signed_narrowed.s = -8;
    old.to_signed.u       = 15;
    old.to_unsigned.n     = -3;
    old.renamed.kept      = 9;
    old.renamed.dropped   = 5;
    old.wider_container.x = 15;
    old.wider_container.y = 3;
    old.gains             = 0xBEEFu;
    old.loses.all         = 0x1234u;
    old.array[0].a        = 7;
    old.array[0].b        = 2;
    old.array[1].a        = 1;
    old.array[1].b        = 7;
    old.array[2].all      = 0x3Fu;
    old.wide.big          = 0xFFFFFFFFFFFFu;
    old.wide.small        = 5;
    old.signed_wide.v     = -(int64_t{1} << 47);
    StoreInstance(RoutineSourceOf(0, MEEM_BLOCK_Bitfields_ID), 0, BytesOf(old));

    ASSERT_TRUE(Migrate(MEEM_BLOCK_Bitfields_ID));

    const auto& cache = MEEM_cache_Bitfields;
    EXPECT_EQ(VALUE(cache.narrowed.a), 3) << "3 -> 2 bits";
    EXPECT_EQ(VALUE(cache.narrowed.b), 6) << "3 -> 5 bits";
    EXPECT_EQ(VALUE(cache.signed_narrowed.s), -4) << "4 -> 3 bits, signed";
    EXPECT_EQ(VALUE(cache.to_signed.u), 7) << "unsigned -> signed";
    EXPECT_EQ(VALUE(cache.to_unsigned.n), 0) << "negative -> unsigned";
    EXPECT_EQ(VALUE(cache.renamed.kept), 9);
    EXPECT_EQ(VALUE(cache.renamed.added), 0xA) << "A new bitfield keeps its default bits";
    EXPECT_EQ(VALUE(cache.wider_container.x), 15);
    EXPECT_EQ(VALUE(cache.wider_container.y), 3);
    EXPECT_EQ(VALUE(cache.gains.all), 0xBEEF) << "Gains its bitfields: the whole value";
    EXPECT_EQ(VALUE(cache.loses), 0xFF) << "Loses its bitfields: the whole value, clamped";
    EXPECT_EQ(VALUE(cache.array[0].a), 3);
    EXPECT_EQ(VALUE(cache.array[0].b), 2);
    EXPECT_EQ(VALUE(cache.array[1].a), 1);
    EXPECT_EQ(VALUE(cache.array[1].b), 7);
    EXPECT_EQ(VALUE(cache.wide.big), (int64_t{1} << 40) - 1) << "48 -> 40 bits of uint64";
    EXPECT_EQ(VALUE(cache.wide.small), 5);
    EXPECT_EQ(VALUE(cache.signed_wide.v), -(int64_t{1} << 39)) << "48 -> 40 bits of int64";
}

TEST_F(ConversionsTest, Bitfields_InRangeKeepTheirValues)
{
    LegacyBitfields old{};
    old.narrowed.a        = 2;
    old.narrowed.b        = 1;
    old.signed_narrowed.s = -3;
    old.to_signed.u       = 5;
    old.to_unsigned.n     = 6;
    old.loses.all         = 0x0042u;
    old.wide.big          = 123456u;
    old.signed_wide.v     = -5;
    StoreInstance(RoutineSourceOf(0, MEEM_BLOCK_Bitfields_ID), 0, BytesOf(old));

    ASSERT_TRUE(Migrate(MEEM_BLOCK_Bitfields_ID));

    const auto& cache = MEEM_cache_Bitfields;
    EXPECT_EQ(VALUE(cache.narrowed.a), 2);
    EXPECT_EQ(VALUE(cache.narrowed.b), 1);
    EXPECT_EQ(VALUE(cache.signed_narrowed.s), -3);
    EXPECT_EQ(VALUE(cache.to_signed.u), 5);
    EXPECT_EQ(VALUE(cache.to_unsigned.n), 6);
    EXPECT_EQ(VALUE(cache.loses), 0x42);
    EXPECT_EQ(VALUE(cache.wide.big), 123456);
    EXPECT_EQ(VALUE(cache.signed_wide.v), -5);

    // The upper ends of the signed bitfields
    old.signed_narrowed.s = 7;
    old.signed_wide.v     = (int64_t{1} << 47) - 1;
    StoreInstance(RoutineSourceOf(0, MEEM_BLOCK_Bitfields_ID), 0, BytesOf(old));

    ASSERT_TRUE(Migrate(MEEM_BLOCK_Bitfields_ID));
    EXPECT_EQ(VALUE(cache.signed_narrowed.s), 3);
    EXPECT_EQ(VALUE(cache.signed_wide.v), (int64_t{1} << 39) - 1);
}

/******************************************************************************/
/*    Multiplicity                                                            */
/******************************************************************************/
#pragma pack(push, 1)
/// @brief The parameters of Multiplicity in the legacy model
struct LegacyMultiplicity {
    uint16_t grow[2];
    uint16_t shrink[4];
    uint8_t  grow_converted[2];
    int32_t  shrink_converted[3];
    uint8_t  to_array;
    uint8_t  to_scalar[3];
};
#pragma pack(pop)

static_assert(sizeof(LegacyMultiplicity) == 30);

TEST_F(ConversionsTest, Multiplicity_TheFirstElementsOfBothSides)
{
    const LegacyMultiplicity old{{0x1111u, 0x2222u}, {10u, 20u, 30u, 40u}, {200u, 0u}, {-1000, 100, 7}, 42u, {250u, 1u, 2u}};
    StoreInstance(RoutineSourceOf(0, MEEM_BLOCK_Multiplicity_ID), 0, BytesOf(old));

    ASSERT_TRUE(Migrate(MEEM_BLOCK_Multiplicity_ID));

    const auto& cache = MEEM_cache_Multiplicity;
    EXPECT_EQ((std::vector<int64_t>{VALUE(cache.grow[0]), VALUE(cache.grow[1]), VALUE(cache.grow[2]), VALUE(cache.grow[3])}),
              (std::vector<int64_t>{0x1111, 0x2222, 3, 4}))
        << "Growing: the new elements keep their defaults";
    EXPECT_EQ((std::vector<int64_t>{VALUE(cache.shrink[0]), VALUE(cache.shrink[1])}), (std::vector<int64_t>{10, 20})) << "Shrinking";
    EXPECT_EQ((std::vector<int64_t>{VALUE(cache.grow_converted[0]), VALUE(cache.grow_converted[1]), VALUE(cache.grow_converted[2])}),
              (std::vector<int64_t>{200, 0, -3}))
        << "Growing, converted";
    EXPECT_EQ((std::vector<int64_t>{VALUE(cache.shrink_converted[0]), VALUE(cache.shrink_converted[1])}), (std::vector<int64_t>{-128, 100}))
        << "Shrinking, converted";
    EXPECT_EQ((std::vector<int64_t>{VALUE(cache.to_array[0]), VALUE(cache.to_array[1]), VALUE(cache.to_array[2])}), (std::vector<int64_t>{42, 8, 9}))
        << "A value becomes an array";
    EXPECT_EQ(VALUE(cache.to_scalar), 250) << "An array becomes a value";
}

/******************************************************************************/
/*    Parameters added, removed and reordered                                 */
/******************************************************************************/
#pragma pack(push, 1)
/// @brief The parameters of Runs in the legacy model
struct LegacyRuns {
    uint8_t  a;
    uint16_t b;
    uint8_t  c[4];
    uint32_t d;
    uint8_t  e;
    uint8_t  f;
    uint8_t  g;
};
#pragma pack(pop)

static_assert(sizeof(LegacyRuns) == 14);

TEST_F(ConversionsTest, Parameters_MatchedByName)
{
    const LegacyRuns old{0xA1u, 0xB2B3u, {1u, 2u, 3u, 4u}, 0xD1D2D3D4u, 0xEEu, 0xF1u, 0xF2u};
    StoreInstance(RoutineSourceOf(0, MEEM_BLOCK_Runs_ID), 0, BytesOf(old));

    ASSERT_TRUE(Migrate(MEEM_BLOCK_Runs_ID));

    // The current order: added (new), b, c, d (copied together), a (moved), f, g (copied together); e is dropped
    const auto& cache = MEEM_cache_Runs;
    EXPECT_EQ(VALUE(cache.added), 90) << "A new parameter keeps its default";
    EXPECT_EQ(VALUE(cache.b), 0xB2B3);
    EXPECT_EQ((std::vector<int64_t>{VALUE(cache.c[0]), VALUE(cache.c[1]), VALUE(cache.c[2]), VALUE(cache.c[3])}), (std::vector<int64_t>{1, 2, 3, 4}));
    EXPECT_EQ(VALUE(cache.d), int64_t{0xD1D2D3D4});
    EXPECT_EQ(VALUE(cache.a), 0xA1);
    EXPECT_EQ(VALUE(cache.f), 0xF1);
    EXPECT_EQ(VALUE(cache.g), 0xF2);
}

/******************************************************************************/
/*    Management types                                                        */
/******************************************************************************/
TEST_F(ConversionsTest, MultiProfile_TheGivenProfileOfTheSource)
{
    const auto source = RoutineSourceOf(0, MEEM_BLOCK_Profiles_ID);
    ASSERT_EQ(source->management_type, MEEM_MGMT_MULTI_PROFILE);
    ASSERT_EQ(source->instance_count, 2u);
    StoreInstance(source, 0, BytesOf(uint16_t{111u}));
    StoreInstance(source, 1, BytesOf(uint16_t{60000u}));

    ASSERT_TRUE(Migrate(MEEM_BLOCK_Profiles_ID, 0, 1));
    EXPECT_EQ(VALUE(MEEM_cache_Profiles.v), 60000);
    EXPECT_EQ(VALUE(MEEM_cache_Profiles.w), 33) << "New: its default";

    ASSERT_TRUE(Migrate(MEEM_BLOCK_Profiles_ID, 0, 0));
    EXPECT_EQ(VALUE(MEEM_cache_Profiles.v), 111);

    // The current block's third profile has no source
    EXPECT_FALSE(Migrate(MEEM_BLOCK_Profiles_ID, 0, 2));
}

TEST_F(ConversionsTest, WearLeveling_TheMostRecentInstanceOfTheSource)
{
    const auto source = RoutineSourceOf(0, MEEM_BLOCK_FromWearLeveling_ID);
    ASSERT_EQ(source->management_type, MEEM_MGMT_WEAR_LEVELING);

    // [sequence counter][v][x]: the instance with counter 5 is the most recent
    StoreInstance(source, 0, {4u, 10u, 1u});
    StoreInstance(source, 1, {5u, 200u, 2u});
    StoreInstance(source, 2, {3u, 30u, 3u});

    ASSERT_TRUE(Migrate(MEEM_BLOCK_FromWearLeveling_ID));
    EXPECT_EQ(VALUE(MEEM_cache_FromWearLeveling.v), 200);
}

TEST_F(ConversionsTest, WearLeveling_TheCacheKeepsItsSequenceCounter)
{
    const auto block_id = MEEM_BLOCK_ToWearLeveling_ID;
    ASSERT_EQ(MEEM_block_config[block_id].management_type, MEEM_MGMT_WEAR_LEVELING);
    StoreInstance(RoutineSourceOf(0, block_id), 0, BytesOf(int16_t{-300}));
    MEEM_block_config[block_id].cache[0] = 0x5Cu;

    ASSERT_TRUE(Migrate(block_id));
    EXPECT_EQ(MEEM_block_config[block_id].cache[0], 0x5Cu);
    EXPECT_EQ(VALUE(MEEM_cache_ToWearLeveling.v), -128);
}

/******************************************************************************/
/*    No valid source                                                         */
/******************************************************************************/
TEST_F(ConversionsTest, FalseWithoutAValidSourceAndTheDefaultsStay)
{
    const auto block_id = MEEM_BLOCK_From_float32_ID;
    StoreTenTimes(block_id, 1.5f);
    CorruptInstance(RoutineSourceOf(0, block_id), 0);

    MEEM_RestoreDefaults(block_id);
    const auto defaults = SnapshotCache(block_id);

    EXPECT_FALSE(Migrate(block_id));
    EXPECT_EQ(SnapshotCache(block_id), defaults);
}

/******************************************************************************/
/*    Dispatcher                                                              */
/******************************************************************************/
TEST_F(ConversionsTest, Dispatcher_TwoLegacyModelsShareTheRoutinesWithTheirOwnSources)
{
    ASSERT_EQ(MEEM_LEGACY_MODEL_COUNT, 2u);

    for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
    {
        EXPECT_NE(RoutineSourceOf(0, block_id)->offset_in_eeprom, RoutineSourceOf(1, block_id)->offset_in_eeprom) << "Block ID " << int(block_id);
    }

    // Each image holds a valid instance of every source, with the defaults
    for (uint8_t k = 0; k < MEEM_LEGACY_MODEL_COUNT; k++)
    {
        LoadLegacyImage(k);
        for (uint8_t block_id = 0; block_id < MEEM_BLOCK_COUNT; block_id++)
        {
            EXPECT_TRUE(Migrate(block_id, k)) << "Legacy model " << int(k) << ", block ID " << int(block_id);
        }
    }

    // The source of legacy model 1, in its own image
    StoreTenTimes(MEEM_BLOCK_From_int32_ID, int32_t{-70000}, 1);
    ASSERT_TRUE(Migrate(MEEM_BLOCK_From_int32_ID, 1));
    EXPECT_EQ(VALUE(MEEM_cache_From_int32.to_int16), INT16_MIN);
    EXPECT_EQ(VALUE(MEEM_cache_From_int32.to_int64), -70000);
}

TEST_F(ConversionsTest, Dispatcher_FalseForAnUnknownModelOrBlock)
{
    EXPECT_FALSE(MEEM_MigrateBlock(MEEM_LEGACY_MODEL_COUNT, MEEM_BLOCK_From_uint8_ID, 0));
    EXPECT_FALSE(MEEM_MigrateBlock(0, MEEM_BLOCK_COUNT, 0));
}

/******************************************************************************/
/*    Scenarios that change parameters, from the legacy image through         */
/*    MEEM_Init(), the rewrites and the commit to a restart, which reads each */
/*    block from its new place                                                */
/******************************************************************************/
TEST_F(ConversionsTest, Scenario_ParametersAddedRemovedAndReordered)
{
    const LegacyRuns old{0xA1u, 0xB2B3u, {1u, 2u, 3u, 4u}, 0xD1D2D3D4u, 0xEEu, 0xF1u, 0xF2u};
    StoreInstance(RoutineSourceOf(0, MEEM_BLOCK_Runs_ID), 0, BytesOf(old));

    MigrateThroughInit();
    Restart();

    const auto& cache = MEEM_cache_Runs;
    EXPECT_EQ(VALUE(cache.added), 90) << "Added: its default";
    EXPECT_EQ(VALUE(cache.b), 0xB2B3);
    EXPECT_EQ((std::vector<int64_t>{VALUE(cache.c[0]), VALUE(cache.c[1]), VALUE(cache.c[2]), VALUE(cache.c[3])}), (std::vector<int64_t>{1, 2, 3, 4}));
    EXPECT_EQ(VALUE(cache.d), int64_t{0xD1D2D3D4});
    EXPECT_EQ(VALUE(cache.a), 0xA1) << "Moved behind d";
    EXPECT_EQ(VALUE(cache.f), 0xF1) << "Behind the removed e";
    EXPECT_EQ(VALUE(cache.g), 0xF2);
}

TEST_F(ConversionsTest, Scenario_TypesChanged)
{
    // Saturated and rounded half away from zero
    constexpr double value = 70000.5;
    StoreTenTimes(MEEM_BLOCK_From_float64_ID, value);

    MigrateThroughInit();
    Restart();

    const auto& cache = MEEM_cache_From_float64;
    EXPECT_CONVERTED(cache, to_uint8, value);
    EXPECT_CONVERTED(cache, to_int8, value);
    EXPECT_CONVERTED(cache, to_uint16, value);
    EXPECT_CONVERTED(cache, to_int16, value);
    EXPECT_CONVERTED(cache, to_uint32, value);
    EXPECT_CONVERTED(cache, to_int32, value);
    EXPECT_CONVERTED(cache, to_uint64, value);
    EXPECT_CONVERTED(cache, to_int64, value);
    EXPECT_CONVERTED(cache, to_float32, value);
    EXPECT_CONVERTED(cache, to_float64, value);
    EXPECT_EQ(VALUE(cache.to_int32), 70001);
}

TEST_F(ConversionsTest, Scenario_MultiplicityChanged)
{
    const LegacyMultiplicity old{{0x1111u, 0x2222u}, {10u, 20u, 30u, 40u}, {200u, 0u}, {-1000, 100, 7}, 42u, {250u, 1u, 2u}};
    StoreInstance(RoutineSourceOf(0, MEEM_BLOCK_Multiplicity_ID), 0, BytesOf(old));

    MigrateThroughInit();
    Restart();

    const auto& cache = MEEM_cache_Multiplicity;
    EXPECT_EQ((std::vector<int64_t>{VALUE(cache.grow[0]), VALUE(cache.grow[1]), VALUE(cache.grow[2]), VALUE(cache.grow[3])}),
              (std::vector<int64_t>{0x1111, 0x2222, 3, 4}))
        << "Growing: the new elements keep their defaults";
    EXPECT_EQ((std::vector<int64_t>{VALUE(cache.shrink[0]), VALUE(cache.shrink[1])}), (std::vector<int64_t>{10, 20})) << "Shrinking";
    EXPECT_EQ((std::vector<int64_t>{VALUE(cache.shrink_converted[0]), VALUE(cache.shrink_converted[1])}), (std::vector<int64_t>{-128, 100}))
        << "Shrinking, converted";
}

TEST_F(ConversionsTest, Scenario_BitfieldsMappedByNameOrConvertedAsAWhole)
{
    // Both kinds of bitfield change: changed with bitfields on both sides, and gained or lost by a parameter
    LegacyBitfields old{};
    old.narrowed.a      = 4;
    old.narrowed.b      = 6;
    old.renamed.kept    = 9;
    old.renamed.dropped = 5;
    old.gains           = 0xBEEFu;
    old.loses.all       = 0x1234u;
    StoreInstance(RoutineSourceOf(0, MEEM_BLOCK_Bitfields_ID), 0, BytesOf(old));

    MigrateThroughInit();
    Restart();

    const auto& cache = MEEM_cache_Bitfields;
    EXPECT_EQ(VALUE(cache.narrowed.a), 3) << "Clamped to 2 bits";
    EXPECT_EQ(VALUE(cache.narrowed.b), 6);
    EXPECT_EQ(VALUE(cache.renamed.kept), 9);
    EXPECT_EQ(VALUE(cache.renamed.added), 0xA) << "A new bitfield keeps its default bits";
    EXPECT_EQ(VALUE(cache.gains.all), 0xBEEF) << "Gains its bitfields: the whole value";
    EXPECT_EQ(VALUE(cache.loses), 0xFF) << "Loses its bitfields: the whole value, clamped";
}

TEST_F(ConversionsTest, Scenario_ManagementTypesChangedByRoutine)
{
    // FromWearLeveling: the most recent instance, the one with sequence counter 5
    const auto from_wl = RoutineSourceOf(0, MEEM_BLOCK_FromWearLeveling_ID);
    StoreInstance(from_wl, 0, {4u, 10u, 1u});
    StoreInstance(from_wl, 1, {5u, 200u, 2u});
    StoreInstance(from_wl, 2, {3u, 30u, 3u});

    // ToWearLeveling: from a basic block into every instance. Profiles: profile by profile in pass 2, the third one new.
    StoreInstance(RoutineSourceOf(0, MEEM_BLOCK_ToWearLeveling_ID), 0, BytesOf(int16_t{-300}));
    StoreInstance(RoutineSourceOf(0, MEEM_BLOCK_Profiles_ID), 0, BytesOf(uint16_t{111u}));
    StoreInstance(RoutineSourceOf(0, MEEM_BLOCK_Profiles_ID), 1, BytesOf(uint16_t{60000u}));

    MigrateThroughInit();

    const std::vector<int64_t> profiles{111, 60000, 0};
    for (uint8_t active = 0; active < 3; active++)
    {
        SCOPED_TRACE("Active profile " + std::to_string(active));
        ON_CALL(user_callbacks_mock, SelectInitiallyActiveProfile(MEEM_BLOCK_Profiles_ID)).WillByDefault(testing::Return(active));

        Restart();
        EXPECT_EQ(VALUE(MEEM_cache_Profiles.v), profiles[active]);
        EXPECT_EQ(VALUE(MEEM_cache_Profiles.w), 33) << "New: its default";
    }
    EXPECT_EQ(VALUE(MEEM_cache_FromWearLeveling.v), 200);
    EXPECT_EQ(VALUE(MEEM_cache_ToWearLeveling.v), -128);
    EXPECT_EQ(MEEM_block_config[MEEM_BLOCK_ToWearLeveling_ID].cache[0], 2u) << "The rewrite wrote the sequence counters 0 and 1";
}
