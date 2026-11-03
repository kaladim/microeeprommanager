#ifndef EEPROM_SIMULATOR_HPP
#define EEPROM_SIMULATOR_HPP

#include <cstdint>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <stdexcept>
#include "MEEM_EEAIF.h"

class EepromSimulator
{
public:
    static constexpr size_t maxEepromSizeBytes{64 * 1024};
    static constexpr uint8_t erased_state{0xFFu};
    std::vector<uint8_t> eeprom;
    std::string file_name;

    /// @brief A write job, as the simulator received it
    struct WriteJob
    {
        size_t offset;
        size_t size;

        bool operator==(const WriteJob &other) const
        {
            return (offset == other.offset) && (size == other.size);
        }
    };

    /// @brief Every write job since the last clear, in the order received, also those that a power cut tore or dropped
    std::vector<WriteJob> write_log;

    EepromSimulator(const std::string &file_name = "./eeprom.bin", size_t eepromSizeBytes = maxEepromSizeBytes) : file_name(file_name)
    {
        eeprom.resize(eepromSizeBytes, erased_state); // Initialize with erased state
    }

    ~EepromSimulator() = default;

    bool read(size_t offset, uint8_t *dest, size_t length)
    {
        length = std::min(length, eeprom.size() - offset);
        if (_powered)
        {
            std::copy(eeprom.begin() + offset, eeprom.begin() + offset + length, dest);
        }
        _status_postpone_counter = status_postpone_ticks;
        return true;
    }

    bool write(size_t offset, const uint8_t *src, size_t length)
    {
        length = std::min(length, eeprom.size() - offset);
        write_log.push_back({offset, length});

        if (_powered)
        {
            if ((_write_jobs_until_power_cut > 0) && (--_write_jobs_until_power_cut == 0))
            {
                // The power fails during this job: only its first bytes reach the EEPROM
                length   = std::min(length, _bytes_before_power_cut);
                _powered = false;
            }
            std::copy(src, src + length, eeprom.begin() + offset);
        }
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
        return (_return_nok_for_next_jobs || !_powered) ? EEAIF_status_t::EEAIF_NOK : EEAIF_status_t::EEAIF_OK;
    }

    /*!
     * \brief Lets the power fail during a later write job, which then writes only its first bytes.
     *        From then on, no job reaches the EEPROM and every job fails, until restore_power(). A test simulates the restart of the device
     *        with restore_power(), MEEM_DeInit() and MEEM_Init().
     * \param job - the write job to tear, counted from now on: 1 is the next one
     * \param bytes_written - how many of its bytes reach the EEPROM: from 0 up to its size
     */
    void cut_power_during_write(unsigned job, size_t bytes_written)
    {
        _write_jobs_until_power_cut = job;
        _bytes_before_power_cut     = bytes_written;
    }

    /*!
     * \brief Powers the EEPROM again after a power cut, and cancels a power cut that hasn't happened yet.
     */
    void restore_power()
    {
        _powered                    = true;
        _write_jobs_until_power_cut = 0;
    }

    bool is_powered() const
    {
        return _powered;
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
     * \brief Loads an image of the whole EEPROM, such as one that the image generator built (meem_add_test_image() in CMake).
     * \throws std::runtime_error if the file can't be read, or its size isn't that of the EEPROM
     */
    void load_image(const std::string &path)
    {
        std::ifstream image(path, std::ios::binary | std::ios::ate);
        if (!image.is_open())
        {
            throw std::runtime_error("EepSim::load_image(): can't open " + path);
        }

        const auto size = static_cast<size_t>(image.tellg());
        if (size != eeprom.size())
        {
            throw std::runtime_error("EepSim::load_image(): " + path + " has " + std::to_string(size) + " bytes, the EEPROM " + std::to_string(eeprom.size()));
        }

        image.seekg(0);
        if (!image.read(reinterpret_cast<char *>(eeprom.data()), static_cast<std::streamsize>(eeprom.size())))
        {
            throw std::runtime_error("EepSim::load_image(): can't read " + path);
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

    /// @brief Power cut simulation
    bool     _powered{true};
    unsigned _write_jobs_until_power_cut{0}; /**< 0: no power cut pending */
    size_t   _bytes_before_power_cut{0};
};

#endif