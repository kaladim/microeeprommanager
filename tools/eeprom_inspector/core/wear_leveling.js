/*
 * mEEM EEPROM Inspector - wear-leveling support.
 *
 * Finds the index of the "most recent" instance in a wear-leveling block from
 * its sequence counters. Faithful port of
 * common/utils.py::find_index_of_most_recent_sequence_counter, including the
 * deliberate `instance_count + 1` loop count.
 *
 * Part of the split `core/` business logic. Classic (non-module) script that
 * contributes to the shared global `EEPROM` namespace. No dependencies.
 */
(function (EEPROM) {
    'use strict';

    function findIndexOfMostRecentSequenceCounter(sequenceCounters) {
        const INVALID_INSTANCE = 0xFF;
        const instanceCount = sequenceCounters.length;

        let sequenceCounterLastValid = INVALID_INSTANCE;
        let min = 0xFF; // Initial min-max thresholds are intentionally inverted!
        let max = 0;
        let minIndex = null;
        let maxIndex = null;
        let rolloverStartIndex = null;
        let rolloverEndIndex = null;
        let i = 0;

        // Find min, max, rollover region.
        // Note: the loop must run +1 more time than instanceCount - critical!
        for (let n = 0; n < instanceCount + 1; n++) {
            const cur = sequenceCounters[i];
            if (cur !== INVALID_INSTANCE) {
                if (cur < min) { min = cur; minIndex = i; }
                if (cur >= max) { max = cur; maxIndex = i; }

                if (
                    rolloverStartIndex === null &&
                    cur < sequenceCounterLastValid &&
                    (sequenceCounterLastValid - cur) >= instanceCount
                ) {
                    rolloverStartIndex = i;
                } else if (
                    rolloverEndIndex === null &&
                    cur > sequenceCounterLastValid &&
                    (cur - sequenceCounterLastValid) >= instanceCount
                ) {
                    rolloverEndIndex = i;
                }

                sequenceCounterLastValid = cur;
            }
            i = (i + 1) % instanceCount;
        }

        // Check for at least 1 valid instance
        if (minIndex === null || maxIndex === null) return null;

        // Check for sequence counter rollover
        if ((max - min) >= instanceCount) {
            // Rollover
            function findIndexOfMaxElement(array, startIndex, loopCount) {
                let localMax = 0;
                let localMaxIndex = null;
                let k = startIndex;
                for (let c = 0; c < loopCount; c++) {
                    const element = array[k];
                    if (element !== INVALID_INSTANCE && element >= localMax) {
                        localMax = element;
                        localMaxIndex = k;
                    }
                    k = (k + 1) % array.length;
                }
                return localMaxIndex;
            }

            const length = (rolloverEndIndex > rolloverStartIndex)
                ? (rolloverEndIndex - rolloverStartIndex)
                : (instanceCount - (rolloverStartIndex - rolloverEndIndex));
            return findIndexOfMaxElement(sequenceCounters, rolloverStartIndex, length);
        }

        // No rollover, return the max
        return maxIndex;
    }

    EEPROM.findIndexOfMostRecentSequenceCounter = findIndexOfMostRecentSequenceCounter;

})(globalThis.EEPROM = globalThis.EEPROM || {});
