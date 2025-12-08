#ifndef EEPROM_SIMULATOR_HPP
#define EEPROM_SIMULATOR_HPP

#include <cstdint>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include "MEEM_EEAIF.h"

class EepromSimulator
{
public:
    static constexpr size_t maxEepromSizeBytes{64 * 1024};
    static constexpr uint8_t erased_state{0xFFu};
    std::vector<uint8_t> eeprom;
    std::string file_name;

    EepromSimulator(const std::string &file_name = "./eeprom.bin", size_t eepromSizeBytes = maxEepromSizeBytes) : file_name(file_name)
    {
        eeprom.resize(eepromSizeBytes, erased_state); // Initialize with erased state
    }

    ~EepromSimulator() = default;

    bool read(size_t offset, uint8_t *dest, size_t length)
    {
        length = std::min(length, eeprom.size() - offset);
        std::copy(eeprom.begin() + offset, eeprom.begin() + offset + length, dest);
        _status_postpone_counter = status_postpone_ticks;
        return true;
    }

    bool write(size_t offset, const uint8_t *src, size_t length)
    {
        length = std::min(length, eeprom.size() - offset);
        std::copy(src, src + length, eeprom.begin() + offset);
        _status_postpone_counter = status_postpone_ticks;
        return true;
    }

    EEAIF_status_t get_status()
    {
        if (_status_postpone_counter > 0)
        {
            _status_postpone_counter--;
            return EEAIF_status_t::EEAIF_BUSY;
        }
        return _return_nok_for_next_jobs ? EEAIF_status_t::EEAIF_NOK : EEAIF_status_t::EEAIF_OK;
    }

    void erase(size_t offset, size_t length)
    {
        length = std::min(length, eeprom.size() - offset);
        std::fill(eeprom.begin() + offset, eeprom.begin() + offset + length, erased_state);
    }
    void erase()
    {
        erase(0, eeprom.size());
    }

    void return_nok_for_next_jobs()
    {
        _return_nok_for_next_jobs = true;
    }
    void return_ok_for_next_jobs()
    {
        _return_nok_for_next_jobs = false;
    }

    /*!
     * \brief Loads the EEPROM image from a file.
     */
    void load()
    {
        // Attempt to load data from file
        std::ifstream inputFile(file_name, std::ios::binary);
        if (inputFile.is_open())
        {
            inputFile.read(reinterpret_cast<char *>(eeprom.data()), eeprom.size());

            // Check to see if the entire file was read. Consider this an error if not all bytes were read.
            if (!inputFile.eof() && inputFile.fail())
            {
                // Could also clear the eeprom data in this case.
                throw std::runtime_error("Failed to read entire file or encountered an error.");
            }
            inputFile.close();
            std::cout << "EepSim::load(): OK" << std::endl;
        }
    }

    /*!
     * \brief Stores the EEPROM image to a file.
     */
    void store() const
    {
        std::ofstream f(file_name, std::ios::binary);
        if (f.is_open())
        {
            f.write(reinterpret_cast<const char *>(eeprom.data()), eeprom.size());
            f.close();
            std::cout << "EepSim::store(): OK" << std::endl;
        }
        else
        {
            std::cerr << "Error opening file for writing: " << file_name << std::endl;
            std::cout << "EepSim::store(): Failed!" << std::endl;
        }
    }

private:
    static constexpr uint8_t status_postpone_ticks{2u};

    /// @brief Simulates time-consuming async read/write
    uint8_t _status_postpone_counter;

    bool _return_nok_for_next_jobs{false};
};

#endif