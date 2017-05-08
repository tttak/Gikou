/*
 * 技巧 (Gikou), a USI shogi (Japanese chess) playing engine.
 * Copyright (C) 2016-2017 Yosuke Demura
 * except where otherwise indicated.
 *
 * This program is free software: you can redistribute it and/or modify
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

#ifndef COMMON_RANGE_H_
#define COMMON_RANGE_H_

/**
 * 範囲を表すためのクラスです.
 */
template<typename Iterator>
class Range {
 public:
  Range(Iterator b, Iterator e) : begin_(b), end_(e) {
  }

  Iterator begin() const {
    return begin_;
  }

  Iterator end() const {
    return end_;
  }

 private:
  Iterator begin_, end_;
};

#endif /* COMMON_RANGE_H_ */
