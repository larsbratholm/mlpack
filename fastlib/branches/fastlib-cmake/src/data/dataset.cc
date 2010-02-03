/* MLPACK 0.2
 *
 * Copyright (c) 2008, 2009 Alexander Gray,
 *                          Garry Boyer,
 *                          Ryan Riegel,
 *                          Nikolaos Vasiloglou,
 *                          Dongryeol Lee,
 *                          Chip Mappus, 
 *                          Nishant Mehta,
 *                          Hua Ouyang,
 *                          Parikshit Ram,
 *                          Long Tran,
 *                          Wee Chin Wong
 *
 * Copyright (c) 2008, 2009 Georgia Institute of Technology
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */
/**
 * @file dataset.cc
 *
 * Implementations for the dataset utilities.
 * 
 * @bug These routines fail when trying to read files linewise that use the Mac
 * eol '\r'.  Both Windows and Unix eol ("\r\n" and '\n') work.  Use the
 * programs 'dos2unix' or 'tr' to convert the '\r's to '\n's.
 *
 */

#include "base/base.h"

#include "dataset.h"


void DatasetFeature::Format(double value, String *result) const {
  if (unlikely(isnan(value))) {
    result->Copy("?");
    return;
  }
  switch (type_) {
    case CONTINUOUS:
      if (floor(value) != value) {
        // non-integer
        result->InitSprintf("%1.17e", value);
      } else {
        // value is actually an integer
        result->InitSprintf("%.17g", value);
      }
      break;
    case INTEGER: result->InitSprintf("%lu", long(value)); break;
    case NOMINAL: result->InitCopy(value_name(int(value))); break;
    #ifdef DEBUG
    default: abort();
    #endif
  }
}

success_t DatasetFeature::Parse(const char *str, double *d) const {
  if (unlikely(str[0] == '?') && unlikely(str[1] == '\0')) {
    *d = DBL_NAN;
    return SUCCESS_PASS;
  }
  switch (type_) {
    case CONTINUOUS: {
        char *end;
        *d = strtod(str, &end);
        if (likely(*end == '\0')) {
          return SUCCESS_PASS;
        } else {
          return SUCCESS_FAIL;
        }
      }
    case INTEGER: {
      int i;
      if (sscanf(str, "%d", &i) == 1) {
        *d = i;
        return SUCCESS_PASS;
      } else {
        return SUCCESS_FAIL;
      }
    }
    case NOMINAL: {
      index_t i;
      for (i = 0; i < value_names_.size(); i++) {
        if (value_names_[i] == str) {
          *d = i;
          return SUCCESS_PASS;
        }
      }
      *d = DBL_NAN;
      return SUCCESS_FAIL;
    }
    default: abort();
  }
}

// DatasetInfo ------------------------------------------------------


void DatasetInfo::InitContinuous(index_t n_features,
    const char *name_in) {
  features_.Init(n_features);

  name_.Copy(name_in);

  for (index_t i = 0; i < n_features; i++) {
    String feature_name;
    feature_name.InitSprintf("feature_%d", int(i));
    features_[i].InitContinuous(feature_name);
  }
}

void DatasetInfo::Init(const char *name_in) {
  features_.Init();
  name_.Copy(name_in);
}

char *DatasetInfo::SkipSpace_(char *s) {
  while (isspace(*s)) {
    s++;
  }

  if (unlikely(*s == '%') || unlikely(*s == '\0')) {
    return s + strlen(s);
  }

  return s;
}

char *DatasetInfo::SkipNonspace_(char *s) {
  while (likely(*s != '\0')
      && likely(*s != '%')
      && likely(*s != ' ')
      && likely(*s != '\t')) {
    s++;
  }

  return s;
}

void DatasetInfo::SkipBlanks_(TextLineReader *reader) {
  while (reader->MoreLines() && *SkipSpace_(reader->Peek().begin()) == '\0') {
    reader->Gobble();
  }
}

success_t DatasetInfo::InitFromArff(TextLineReader *reader,
    const char *filename) {
  success_t result = SUCCESS_PASS;

  Init(filename);

  while (1) {
    SkipBlanks_(reader);

    String *peeked = &reader->Peek();
    ArrayList<String> portions;

    portions.Init();
    peeked->Split(0, " \t", "%", 3, &portions);

    if (portions.size() == 0) {
      /* empty line */
    } else if (portions[0][0] != '@') {
      reader->Error("ARFF: Unexpected @command.  Did you forget @data?");
      result = SUCCESS_FAIL;
      break;
    } else {
      if (portions[0].EqualsNoCase("@relation")) {
        if (portions.size() < 2) {
          reader->Error("ARFF: @relation requires name");
          result = SUCCESS_FAIL;
        } else {
          set_name(portions[1]);
        }
      } else if (portions[0].EqualsNoCase("@attribute")) {
        if (portions.size() < 3) {
          reader->Error("ARFF: @attribute requires name and type.");
          result = SUCCESS_FAIL;
        } else {
          if (portions[2][0] == '{') { //}
            DatasetFeature *feature = &features_.PushBack();

            feature->InitNominal(portions[1]);
            // TODO: Doesn't support values with spaces {
            portions[2].Split(1, ", \t", "}%", 0, &feature->value_names());
          } else {
            String type(portions[2]);
            //portions[2].Trim(" \t", &type);
            if (type.EqualsNoCase("numeric")
                || type.EqualsNoCase("real")) {
              features_.PushBack().InitContinuous(portions[1]);
            } else if (type.EqualsNoCase("integer")) {
              features_.PushBack().InitInteger(portions[1]);
            } else {
              reader->Error(
                  "ARFF: Only support 'numeric', 'real', and {nominal}.");
              result = SUCCESS_FAIL;
            }
          }
        }
      } else if (portions[0].EqualsNoCase("@data")) {
        /* Done! */
        reader->Gobble();
        break;
      } else {
        reader->Error("ARFF: Expected @relation, @attribute, or @data.");
        result = SUCCESS_FAIL;
        break;
      }
    }

    reader->Gobble();
  }

  return result;
}

success_t DatasetInfo::InitFromCsv(TextLineReader *reader,
    const char *filename) {
  ArrayList<String> headers;
  bool nonnumeric = false;

  Init(filename);

  headers.Init();
  reader->Peek().Split(", \t", &headers);

  if (headers.size() == 0) {
    reader->Error("Trying to parse empty file as CSV.");
    return SUCCESS_FAIL;
  }

  // Try to auto-detect if there is a header row
  for (index_t i = 0; i < headers.size(); i++) {
    char *end;

    (void) strtod(headers[i], &end);

    if (end != headers[i].end()) {
      nonnumeric = true;
      break;
    }
  }

  if (nonnumeric) {
    for (index_t i = 0; i < headers.size(); i++) {
      features_.PushBack().InitContinuous(headers[i]);
    }
    reader->Gobble();
  } else {
    for (index_t i = 0; i < headers.size(); i++) {
      String name;
      name.InitSprintf("feature%"LI, i);
      features_.PushBack().InitContinuous(name);
    }
  }

  return SUCCESS_PASS;
}

success_t DatasetInfo::InitFromFile(TextLineReader *reader,
    const char *filename) {
  SkipBlanks_(reader);

  char *first_line = SkipSpace_(reader->Peek().begin());

  if (!first_line) {
    Init();
    reader->Error("Could not parse the first line.");
    return SUCCESS_FAIL;
  } else if (*first_line == '@') {
    /* Okay, it's ARFF. */
    return InitFromArff(reader, filename);
  } else {
    /* It's CSV.  We'll try to see if there are headers. */
    return InitFromCsv(reader, filename);
  }
}

index_t Dataset::n_labels() const {
  index_t i = 0;
  index_t label_row_idx = matrix_.n_rows() - 1; // the last row is for labels
  index_t n_labels = 0;

  double current_label;
  
  ArrayList<double> labels_list;
  labels_list.Init();
  labels_list.PushBack() = matrix_.get(label_row_idx,0); 
  n_labels++;

  for (i = 1; i < matrix_.n_cols(); i++) {
    current_label = matrix_.get(label_row_idx,i);
    index_t j = 0;
    for (j = 0; j < n_labels; j++) {
      if (current_label == labels_list[j]) {
        break;
      }
    }
    if (j == n_labels) { // new label
      labels_list.PushBack() = current_label;
      n_labels++;
    }
  }
  labels_list.Clear();
  return n_labels;
}

void Dataset::GetLabels(ArrayList<double> &labels_list,
                        ArrayList<index_t> &labels_index,
                        ArrayList<index_t> &labels_ct,
                        ArrayList<index_t> &labels_startpos) const {
  index_t i = 0;
  index_t label_row_idx = matrix_.n_rows() - 1; // the last row is for labels
  index_t n_points = matrix_.n_cols();
  index_t n_labels = 0;

  double current_label;

  // these Arraylists need initialization before-hand
  labels_list.Renew();
  labels_index.Renew();
  labels_ct.Renew();
  labels_startpos.Renew();

  labels_index.Init(n_points);
  labels_list.Init();
  labels_ct.Init();
  labels_startpos.Init();

  ArrayList<index_t> labels_temp;
  labels_temp.Init(n_points);
  labels_temp[0] = 0;

  labels_list.PushBack() = matrix_.get(label_row_idx,0);
  labels_ct.PushBack() = 1;
  n_labels++;

  for (i = 1; i < n_points; i++) {
    current_label = matrix_.get(label_row_idx, i);
    index_t j = 0;
    for (j = 0; j < n_labels; j++) {
      if (current_label == labels_list[j]) {
        labels_ct[j]++;
	break;
      }
    }
    labels_temp[i] = j;
    if (j == n_labels) { // new label
      labels_list.PushBack() = current_label; // add new label to list
      labels_ct.PushBack() = 1;
      n_labels++;
    }
  }
  
  labels_startpos.PushBack() = 0;
  for(i = 1; i < n_labels; i++){
    labels_startpos.PushBack() = labels_startpos[i-1] + labels_ct[i-1];
  }

  for(i = 0; i < n_points; i++) {
    labels_index[labels_startpos[labels_temp[i]]] = i;
    labels_startpos[labels_temp[i]]++;
  }

  labels_startpos[0] = 0;
  for(i = 1; i < n_labels; i++)
    labels_startpos[i] = labels_startpos[i-1] + labels_ct[i-1];

  labels_temp.Clear();
}

bool DatasetInfo::is_all_continuous() const {
  for (index_t i = 0; i < features_.size(); i++) {
    if (features_[i].type() != DatasetFeature::CONTINUOUS) {
      return false;
    }
  }

  return true;
}

success_t DatasetInfo::ReadMatrix(TextLineReader *reader, Matrix *matrix) const {
  ArrayList<double> linearized;
  index_t n_features = this->n_features();
  index_t n_points = 0;
  success_t retval = SUCCESS_PASS;
  bool is_done;

  linearized.Init();
  
  do {
    double *point = linearized.PushBackRaw(n_features);
    retval = ReadPoint(reader, point, &is_done);
    n_points++;
  } while (!is_done && !FAILED(retval));

  if (!FAILED(retval)) {
    DEBUG_ASSERT(linearized.size() == n_features * n_points);
    DEBUG_ASSERT(linearized.size() >= n_features);
    DEBUG_ASSERT(linearized.size() % n_features == 0);
    n_points--;
    linearized.Resize(n_features * n_points);
  }

  linearized.Trim();

  matrix->Own(linearized.ReleasePtr(), n_features, n_points);

  return retval;
}

success_t DatasetInfo::ReadPoint(TextLineReader *reader, double *point,
    bool *is_done) const {
  index_t n_features = this->n_features();
  char *pos;

  *is_done = false;

  for (;;) {
    if (!reader->MoreLines()) {
      *is_done = true;
      return SUCCESS_PASS;
    }

    pos = reader->Peek().begin();

    while (*pos == ' ' || *pos == '\t' || *pos == ',') {
      pos++;
    }

    if (unlikely(*pos == '\0' || *pos == '%')) {
      reader->Gobble();
    } else {
      break;
    }
  }

  for (index_t i = 0; i < n_features; i++) {
    char *next;

    while (*pos == ' ' || *pos == '\t' || *pos == ',') {
      pos++;
    }

    if (unlikely(*pos == '\0')) {
      for (char *s = reader->Peek().begin(); s < pos; s++) {
        if (!*s) {
          *s = ',';
        }
      }
      reader->Error("I am expecting %"LI"d entries per row, "
          "but this line has only %"LI"d.",
          n_features, i);
      return SUCCESS_FAIL;
    }

    next = pos;
    while (*next != '\0' && *next != ' ' && *next != '\t' && *next != ','
        && *next != '%') {
      next++;
    }

    if (*next != '\0') {
      char c = *next;
      *next = '\0';
      if (c != '%') {
        next++;
      }
    }

    if (!PASSED(features_[i].Parse(pos, &point[i]))) {
      char *end = reader->Peek().end();
      String tmp;
      tmp.Copy(pos);
      for (char *s = reader->Peek().begin(); s < next && s < end; s++) {
        if (*s == '\0') {
          *s = ',';
        }
      }
      reader->Error("Invalid parse: [%s]", tmp.c_str());
      return SUCCESS_FAIL;
    }

    pos = next;
  }

  while (*pos == ' ' || *pos == '\t' || *pos == ',') {
    pos++;
  }

  if (*pos != '\0') {
    for (char *s = reader->Peek().begin(); s < pos; s++) {
      if (*s == '\0') {
        *s = ',';
      }
    }
    reader->Error("Extra junk on line.");
    return SUCCESS_FAIL;
  }

  reader->Gobble();

  return SUCCESS_PASS;
}


void DatasetInfo::WriteArffHeader(TextWriter *writer) const {
  writer->Printf("@relation %s\n", name_.c_str());

  for (index_t i = 0; i < features_.size(); i++) {
    const DatasetFeature *feature = &features_[i];
    writer->Printf("@attribute %s ", feature->name().c_str());
    if (feature->type() == DatasetFeature::NOMINAL) {
      writer->Printf("{");
      for (index_t v = 0; v < feature->n_values(); v++) {
        if (v != 0) {
          writer->Write(",");
        }
        writer->Write(feature->value_name(v).c_str());
      }
      writer->Printf("}");
    } else {
      writer->Write("real");
    }
    writer->Write("\n");
  }
  writer->Printf("@data\n");
}

void DatasetInfo::WriteCsvHeader(const char *sep, TextWriter *writer) const {
  for (index_t i = 0; i < features_.size(); i++) {
    if (i != 0) {
      writer->Write(sep);
    }
    writer->Write(features_[i].name().c_str());
  }
  writer->Write("\n");
}

void DatasetInfo::WriteMatrix(const Matrix& matrix, const char *sep,
    TextWriter *writer) const {
  for (index_t i = 0; i < matrix.n_cols(); i++) {
    for (index_t f = 0; f < features_.size(); f++) {
      if (f != 0) {
        writer->Write(sep);
      }
      String str;
      features_[f].Format(matrix.get(f, i), &str);
      writer->Write(str);
    }
    writer->Write("\n");
  }
}

// Dataset ------------------------------------------------------------------

success_t Dataset::InitFromFile(const char *fname) {
  TextLineReader reader;

  if (PASSED(reader.Open(fname))) {
    return InitFromFile(&reader, fname);
  } else {
    matrix_.Init(0, 0);
    info_.Init();
    NONFATAL("Could not open file '%s' for reading.", fname);
    return SUCCESS_FAIL;
  }
}

success_t Dataset::InitFromFile(TextLineReader *reader,
    const char *filename) {
  success_t result;

  result = info_.InitFromFile(reader, filename);
  if (PASSED(result)) {
    result = info_.ReadMatrix(reader, &matrix_);
  } else {
    matrix_.Init(0, 0);
  }

  return result;
}


success_t Dataset::WriteCsv(const char *fname, bool header) const {
  TextWriter writer;

  if (!PASSED(writer.Open(fname))) {
    NONFATAL("Couldn't open '%s' for writing.", fname);
    return SUCCESS_FAIL;
  } else {
    if (header) {
      info_.WriteCsvHeader(",\t", &writer);
    }
    info_.WriteMatrix(matrix_, ",\t", &writer);
    return writer.Close();
  }
}

success_t Dataset::WriteArff(const char *fname) const {
  TextWriter writer;

  if (!PASSED(writer.Open(fname))) {
    NONFATAL("Couldn't open '%s' for writing.", fname);
    return SUCCESS_FAIL;
  } else {
    info_.WriteArffHeader(&writer);
    info_.WriteMatrix(matrix_, ",", &writer);
    return writer.Close();
  }
}

void Dataset::SplitTrainTest(int folds, int fold_number,
    const ArrayList<index_t>& permutation,
    Dataset *train, Dataset *test) const {
  index_t n_test = (n_points() + folds - fold_number - 1) / folds;
  index_t n_train = n_points() - n_test;

  train->InitBlank();
  train->info().InitCopy(info());

  test->InitBlank();
  test->info().InitCopy(info());

  train->matrix().Init(n_features(), n_train);
  test->matrix().Init(n_features(), n_test);

  index_t i_train = 0;
  index_t i_test = 0;
  index_t i_orig = 0;

  for (i_orig = 0; i_orig < n_points(); i_orig++) {
    double *dest;

    if (unlikely((i_orig - fold_number) % folds == 0)) {
      dest = test->matrix().GetColumnPtr(i_test);
      i_test++;
    } else {
      dest = train->matrix().GetColumnPtr(i_train);
      i_train++;
    }

    mem::Copy(dest,
        this->matrix().GetColumnPtr(permutation[i_orig]),
        n_features());
  }

  DEBUG_ASSERT(i_train == train->n_points());
  DEBUG_ASSERT(i_test == test->n_points());
}

success_t data::Load(const char *fname, Matrix *matrix) {
  Dataset dataset;
  success_t result = dataset.InitFromFile(fname);
  matrix->Own(&dataset.matrix());
  return result;
}

success_t data::Save(const char *fname, const Matrix& matrix) {
  Dataset dataset;
  dataset.AliasMatrix(matrix);
  return dataset.WriteCsv(fname);
}

