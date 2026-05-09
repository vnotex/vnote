#ifndef URLUTILS_H
#define URLUTILS_H

#include <QString>

namespace vnotex {

struct UrlFragmentResult {
  QString path;
  QString fragment;
};

// Split a URL string at the first '#' into path and fragment.
// The fragment is URL-decoded via QUrl::fromPercentEncoding().
// If there is no '#', fragment is empty.
// If the string starts with '#', path is empty.
UrlFragmentResult splitUrlFragment(const QString &p_url);

} // namespace vnotex

#endif // URLUTILS_H
