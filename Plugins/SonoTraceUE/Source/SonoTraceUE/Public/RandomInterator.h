/* RandomIterator - (c) Tyler Burdsall 2018
 * https://tylerburdsall.medium.com/generating-unique-random-and-evenly-distributed-numbers-in-ascending-order-with-c-and-in-o-1-9dd5be0c0fcd
 * 
 * A C++ class that can generate random, unique, and evenly-distributed
 * numbers with O(1) memory space.
 */

#pragma once
#include <vector>
#include <random>
#include <iostream>
#include <stdexcept>

using std::mt19937_64;
using std::random_device;
using std::uniform_int_distribution;
using std::vector;
using std::cout;
using std::cerr;
using std::endl;
using std::out_of_range;

class FRandomIterator
{
    public:
        /* Constructor for RandomIterator class.
         * Parameters:
         *     - Amount => Number of numbers to generate
         *     - Min => Minimum number in range to generate
         *     - Max => Maximum number in range to generate
         *
         * The constructor also instantiates the variable gen
         * with a new random_device.
         */
        FRandomIterator(const unsigned long long &Amount, const unsigned long long &Min, const unsigned long long &Max): Gen(random_device()())

        {
            Floor = Min;
            NumLeft = Amount;
            LastK = Min;
            N = Max;
        }

        // Return a bool to determine if there are any numbers left to generate
        bool HasNext() const
        {
            return NumLeft > 0;
        }

        // Generate the next random number
        unsigned long long Next()
        {
            if (NumLeft > 0)
            {
                // Partition the range of numbers to generate from
                const unsigned long long RangeSize = (N - LastK) / NumLeft;
                
                // Initialize random generator
                uniform_int_distribution Rnd(Floor, RangeSize);

                // Generate random number
                const unsigned long long R = Rnd(Gen) + LastK + 1;

                // Set LastK to r so that r is not generated again
                LastK = R;
                NumLeft--;
                return R;
            }
            throw out_of_range("Exceeded Amount of random numbers to generate.");
        }
    private:
            unsigned long long Floor;
            unsigned long long N;
            unsigned long long LastK;
            unsigned long long NumLeft;
            mt19937_64 Gen;
};