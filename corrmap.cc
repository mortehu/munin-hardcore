#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

typedef std::map<time_t, double>         datamap;
typedef std::map<std::string, datamap>   sourcemap;
typedef std::map<std::string, sourcemap> subjectmap;
typedef std::map<std::string, subjectmap> hostmap;

typedef std::vector<std::pair <double, double> > valuevector;

static hostmap m;

extern "C" void
corrmap_add (const char *host, const char *subject, const char *source, time_t t, double v)
{
  m[host][subject][source][t] = v;
}

extern "C" void
corrmap_process ()
{
  valuevector vv;

  hostmap::const_iterator host;
  subjectmap::const_iterator subject;
  sourcemap::const_iterator source_a, source_b;
  datamap::const_iterator data_a, data_b;
  valuevector::const_iterator v;

  fprintf (stderr, "Processing...\n");

  for (host = m.begin(); host != m.end(); ++host)
    {
      for (subject = host->second.begin(); subject != host->second.end(); ++subject)
        {
          for (source_a = subject->second.begin(); source_a != subject->second.end(); ++source_a)
            {
              source_b = source_a;

              for (++source_b; source_b != subject->second.end(); ++source_b)
                {
                  double sum_a = 0, sum_b = 0;
                  double sum_sqerr_a = 0, sum_sqerr_b = 0;
                  double sum_prod = 0;
                  double sample_stdev_a, sample_stdev_b;
                  double avg_a, avg_b;

                  vv.clear();

                  data_a = source_a->second.begin();
                  data_b = source_b->second.begin();

                  while (data_a != source_a->second.end() && data_b != source_b->second.end())
                    {
                      if (data_a->first < data_b->first)
                        {
                          ++data_a;

                          continue;
                        }

                      if (data_b->first < data_a->first)
                        {
                          ++data_b;

                          continue;
                        }

                      sum_a += data_a->second;
                      sum_b += data_b->second;

                      vv.push_back(valuevector::value_type(data_a->second, data_b->second));

                      assert (vv.size() < 1000);

                      ++data_a;
                      ++data_b;
                    }

                  if (vv.size() < 2)
                    continue;

                  avg_a = sum_a / vv.size();
                  avg_b = sum_b / vv.size();

                  for (v = vv.begin(); v != vv.end(); ++v)
                    {
                      sum_sqerr_a += (v->first - avg_a) * (v->first - avg_a);
                      sum_sqerr_b += (v->second - avg_b) * (v->second - avg_b);
                      sum_prod += (v->first - avg_a) * (v->second - avg_b);
                    }

                  if (!sum_prod)
                    continue;

                  sample_stdev_a = sqrt (sum_sqerr_a / (vv.size() - 1));
                  sample_stdev_b = sqrt (sum_sqerr_b / (vv.size() - 1));

                  assert (!isnan (sum_a));
                  assert (!isnan (sum_b));
                  assert (!isnan (sum_sqerr_a));
                  assert (!isnan (sum_sqerr_b));
                  assert (!isnan (sum_prod));
                  assert (!isnan (sample_stdev_a));
                  assert (!isnan (sample_stdev_b));
                  assert (!isnan (avg_a));
                  assert (!isnan (avg_b));

                  printf ("%g\t%s\t%s\t%s\n",
                          sum_prod / ((vv.size() - 1) * sample_stdev_a * sample_stdev_b),
                          subject->first.c_str(),
                          source_a->first.c_str(),
                          source_b->first.c_str());

                  fflush (stdout);
                }
            }
        }
    }
}
