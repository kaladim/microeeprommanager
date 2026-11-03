#ifndef MEEM_CRCIF_MOCK_HPP
#define MEEM_CRCIF_MOCK_HPP

#include <gmock/gmock.h>
#include "MEEM_CRCIF.h"
#include "reference_crc16.hpp"

/*!
 * \brief Model of a CRC unit, built on the reference CRC.
 *        It calculates when it delivers the result, like a DMA-driven unit that reads the data meanwhile,
 *        so a caller that changes the data before the result is in gets a wrong CRC.
 */
class CrcUnitModel
{
  public:
    uint16_t initial_value{0xFFFF}; /**< 0xFFFF for CRC-16/IBM-3740. Another value makes a misconfigured unit. */
    uint8_t  result_delay{0};       /**< Polls of each calculation answered with CRCIF_BUSY before the result */

    CRCIF_status_t BeginCalculation(const uint8_t* data, uint16_t size)
    {
        _data        = data;
        _size        = size;
        _polls_left  = result_delay;
        _calculating = true;
        return CRCIF_OK;
    }

    CRCIF_status_t GetResult(uint16_t* crc)
    {
        if (!_calculating)
        {
            return CRCIF_NOK; /* No calculation was accepted */
        }
        if (_polls_left > 0)
        {
            _polls_left--;
            return CRCIF_BUSY;
        }

        *crc         = ReferenceCrc16(_data, _size, initial_value);
        _calculating = false;
        return CRCIF_OK;
    }

  private:
    const uint8_t* _data{nullptr};
    uint16_t       _size{0};
    uint8_t        _polls_left{0};
    bool           _calculating{false};
};

/*!
 * \brief Mock of the CRC unit driver. By default, it works like the model.
 * \note  The static instance pointer is managed by the test fixture (SetUp/TearDown).
 *        Without an instance, the driver of the test configuration works like a default model.
 */
class MockCRCIF
{
  public:
    // Static instance pointer - set by test fixture
    static MockCRCIF* instance;

    CrcUnitModel model{};

    MOCK_METHOD(CRCIF_status_t, BeginCalculation, (const uint8_t* data, uint16_t size));
    MOCK_METHOD(CRCIF_status_t, GetResult, (uint16_t* crc));

    MockCRCIF()
    {
        ON_CALL(*this, BeginCalculation(testing::_, testing::_))
            .WillByDefault([this](const uint8_t* data, uint16_t size) { return model.BeginCalculation(data, size); });
        ON_CALL(*this, GetResult(testing::_)).WillByDefault([this](uint16_t* crc) { return model.GetResult(crc); });
    }
};

#endif // MEEM_CRCIF_MOCK_HPP
