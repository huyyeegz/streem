#include "strm.h"
#include <math.h>

int
strm_array_p(strm_value v)
{
  switch (strm_value_tag(v)) {
  case STRM_TAG_ARRAY:
  case STRM_TAG_STRUCT:
    return TRUE;
  default:
    return FALSE;
  }
}

strm_array
strm_ary_new(const strm_value* p, strm_int len)
{
  struct strm_array* ary;
  strm_value *buf;

  ary = malloc(sizeof(struct strm_array)+sizeof(strm_value)*len);
  buf = (strm_value*)&ary[1];

  if (p) {
    memcpy(buf, p, sizeof(strm_value)*len);
  }
  else {
    memset(buf, 0, sizeof(strm_value)*len);
  }
  ary->ptr = buf;
  ary->len = len;
  ary->ns = NULL;
  ary->headers = strm_ary_null;
  return STRM_TAG_ARRAY | (strm_value)((intptr_t)ary & STRM_VAL_MASK);
}

int
strm_ary_eq(strm_array a, strm_array b)
{
  strm_int i, len;

  if (a == b) return TRUE;
  if (strm_ary_len(a) != strm_ary_len(b)) return FALSE;
  for (i=0, len=strm_ary_len(a); i<len; i++) {
    if (!strm_value_eq(strm_ary_ptr(a)[i], strm_ary_ptr(b)[i])) {
      return FALSE;
    }
  }
  return TRUE;
}

struct strm_array*
strm_ary_struct(strm_value v)
{
  return (struct strm_array*)strm_value_vptr(v);
}

strm_state* strm_array_ns;

static int
ary_length(strm_stream* strm, int argc, strm_value* args, strm_value* ret)
{
  strm_int len;

  if (argc != 1) return STRM_NG;
  len = strm_ary_len(strm_value_ary(args[0]));
  *ret = strm_int_value(len);
  return STRM_OK;
}

static int
ary_sum_avg(strm_stream* strm, int argc, strm_value* args, strm_value* ret, int avg)
{
  if (argc != 1) {
    strm_raise(strm, "wrong number of arguments");
    return STRM_NG;
  }
  else {
    strm_array values = strm_value_ary(args[0]);
    int i, len = strm_ary_len(values);
    strm_value* v = strm_ary_ptr(values);
    double sum = 0;
    double c = 0;

    for (i=0; i<len; i++) {
      double y = strm_value_flt(v[i]) - c;
      double t = sum + y;
      c = (t - sum) - y;
      sum =  t;
    }
    if (avg) {
      *ret = strm_flt_value(sum/len);
    }
    else {
      *ret = strm_flt_value(sum);
    }
    return STRM_OK;
  }
}

static int
ary_sum(strm_stream* strm, int argc, strm_value* args, strm_value* ret)
{
  return ary_sum_avg(strm, argc, args, ret, FALSE);
}

static int
ary_avg(strm_stream* strm, int argc, strm_value* args, strm_value* ret)
{
  return ary_sum_avg(strm, argc, args, ret, TRUE);
}

static int
ary_var_stdev(strm_stream* strm, int argc, strm_value* args, strm_value* ret, int stdev)
{
  if (argc != 1) {
    strm_raise(strm, "wrong number of arguments");
    return STRM_NG;
  }
  else {
    strm_array values = strm_value_ary(args[0]);
    int i, len = strm_ary_len(values);
    strm_value* v = strm_ary_ptr(values);
    double s1, s2;

    s1 = s2 = 0.0;
    for (i=0; i<len; i++) {
      double x = strm_value_flt(v[i]);
      x -= s1;
      s1 += x/(i+1);
      s2 += i * x * x / (i+1);
    }
    s2 = s2 / (i-1);
    if (stdev) {
      s2 = sqrt(s2);
    }
    *ret = strm_flt_value(s2);
    return STRM_OK;
  }
}

static int
ary_stdev(strm_stream* strm, int argc, strm_value* args, strm_value* ret)
{
  return ary_var_stdev(strm, argc, args, ret, TRUE);
}

static int
ary_var(strm_stream* strm, int argc, strm_value* args, strm_value* ret)
{
  return ary_var_stdev(strm, argc, args, ret, FALSE);
}

static int
ary_minmax(strm_stream* strm, int argc, strm_value* args, strm_value* ret, int min)
{
  strm_array values;
  strm_value func = strm_nil_value();
  int i, len;
  strm_value* v;
  strm_value e, val;
  double num, f;

  switch (argc) {
  case 2:                       /* min(ary,func)/max(ary,func) */
    func = args[1];
    /* fall through */
  case 1:                       /* min(ary)/max(ary) */
    values = args[0];
    break;
  default:
    strm_raise(strm, "wrong number of arguments");
    return STRM_NG;
  }

  len = strm_ary_len(values);
  if (len == 0) {
    *ret = strm_nil_value();
    return STRM_OK;
  }
  v = strm_ary_ptr(values);
  val = v[0];
  if (!strm_nil_p(func)) {
    if (strm_funcall(strm, func, 1, &v[0], &e) == STRM_NG) {
      return STRM_NG;
    }
  }
  else {
    e = v[0];
  }
  num = strm_value_flt(e);
  for (i=1; i<len; i++) {
    if (!strm_nil_p(func)) {
      if (strm_funcall(strm, func, 1, &v[i], &e) == STRM_NG) {
        return STRM_NG;
      }
    }
    else {
      e = v[0];
    }
    f = strm_value_flt(e);
    if (min) {
      if (num > f) {
        num = f;
        val = v[i];
      }
    }
    else {
      if (num < f) {
        num = f;
        val = v[i];
      }
    }
  }
  *ret = val;
  return STRM_OK;
}

static int
ary_min(strm_stream* strm, int argc, strm_value* args, strm_value* ret)
{
  return ary_minmax(strm, argc, args, ret, TRUE);
}

static int
ary_max(strm_stream* strm, int argc, strm_value* args, strm_value* ret)
{
  return ary_minmax(strm, argc, args, ret, FALSE);
}

void
strm_array_init(strm_state* state)
{
  strm_array_ns = strm_ns_new(NULL);
  strm_var_def(strm_array_ns, "length", strm_cfunc_value(ary_length));
  strm_var_def(strm_array_ns, "sum", strm_cfunc_value(ary_sum));
  strm_var_def(strm_array_ns, "average", strm_cfunc_value(ary_avg));
  strm_var_def(strm_array_ns, "stdev", strm_cfunc_value(ary_stdev));
  strm_var_def(strm_array_ns, "variance", strm_cfunc_value(ary_var));
  strm_var_def(strm_array_ns, "min", strm_cfunc_value(ary_min));
  strm_var_def(strm_array_ns, "max", strm_cfunc_value(ary_max));
}
