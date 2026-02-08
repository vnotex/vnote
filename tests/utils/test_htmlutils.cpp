// test_htmlutils.cpp - Comprehensive tests for vnotex::HtmlUtils class
#include <QtTest>

#include <utils/htmlutils.h>

using namespace vnotex;

namespace tests {

class TestHtmlUtils : public QObject {
  Q_OBJECT

private slots:
  // escapeHtml tests
  void testEscapeHtml_data();
  void testEscapeHtml();

  // unicodeEncode tests
  void testUnicodeEncode_data();
  void testUnicodeEncode();

  // hasOnlyImgTag tests
  void testHasOnlyImgTag_data();
  void testHasOnlyImgTag();
};

// =============================================================================
// escapeHtml tests
// =============================================================================
void TestHtmlUtils::testEscapeHtml_data() {
  QTest::addColumn<QString>("input");
  QTest::addColumn<QString>("expected");

  // Basic escaping
  QTest::newRow("no_special") << "hello world" << "hello world";
  QTest::newRow("ampersand") << "a & b" << "a &amp; b";
  QTest::newRow("less_than") << "a < b" << "a &lt; b";
  QTest::newRow("greater_than") << "a > b" << "a &gt; b";

  // Combined - this tests the ORDER of replacements
  // If & is replaced AFTER < and >, then "&lt;" becomes "&amp;lt;" - WRONG
  // Correct order: & first, then < and >
  QTest::newRow("all_special") << "<a & b>" << "&lt;a &amp; b&gt;";
  QTest::newRow("html_tag") << "<div>content</div>" << "&lt;div&gt;content&lt;/div&gt;";

  // Edge cases
  QTest::newRow("empty") << "" << "";
  QTest::newRow("only_amp") << "&" << "&amp;";
  QTest::newRow("only_lt") << "<" << "&lt;";
  QTest::newRow("only_gt") << ">" << "&gt;";
  QTest::newRow("multiple_amp") << "a && b &&& c" << "a &amp;&amp; b &amp;&amp;&amp; c";

  // Already escaped - should double-escape (& becomes &amp;)
  QTest::newRow("already_escaped_amp") << "&amp;" << "&amp;amp;";
  QTest::newRow("already_escaped_lt") << "&lt;" << "&amp;lt;";
}

void TestHtmlUtils::testEscapeHtml() {
  QFETCH(QString, input);
  QFETCH(QString, expected);

  QCOMPARE(HtmlUtils::escapeHtml(input), expected);
}

// =============================================================================
// unicodeEncode tests
// =============================================================================
void TestHtmlUtils::testUnicodeEncode_data() {
  QTest::addColumn<QString>("input");
  QTest::addColumn<QString>("expected");

  // ASCII stays as-is
  QTest::newRow("ascii_only") << "hello" << "hello";
  QTest::newRow("numbers") << "12345" << "12345";
  QTest::newRow("symbols") << "!@#$%" << "!@#$%";

  // Unicode characters (> 255) get encoded as &#xxx;
  QTest::newRow("chinese") << "中" << "&#20013;";
  QTest::newRow("chinese_mixed") << "a中b" << "a&#20013;b";
  QTest::newRow("emoji") << "😀" << "&#55357;&#56832;"; // Surrogate pair

  // Extended ASCII (128-255) stays as-is
  QTest::newRow("extended_ascii") << QString::fromUtf8("é") << QString::fromUtf8("é");

  // Empty
  QTest::newRow("empty") << "" << "";
}

void TestHtmlUtils::testUnicodeEncode() {
  QFETCH(QString, input);
  QFETCH(QString, expected);

  QCOMPARE(HtmlUtils::unicodeEncode(input), expected);
}

// =============================================================================
// hasOnlyImgTag tests
// =============================================================================
void TestHtmlUtils::testHasOnlyImgTag_data() {
  QTest::addColumn<QString>("html");
  QTest::addColumn<bool>("expected");

  // Only img tags - should return true
  QTest::newRow("simple_img") << "<img src='a.png'>" << true;
  QTest::newRow("multiple_img") << "<img src='a.png'><img src='b.png'>" << true;

  // Contains p/span/div - should return false
  QTest::newRow("with_p") << "<p class='test'>text</p>" << false;
  QTest::newRow("with_span") << "<span style='color:red'>text</span>" << false;
  QTest::newRow("with_div") << "<div id='container'>content</div>" << false;
  QTest::newRow("img_in_p") << "<p ><img src='a.png'></p>" << false;

  // Empty
  QTest::newRow("empty") << "" << true;

  // Note: The regex looks for "<p ", "<span ", "<div " with space
  // So "<p>" without space would return true (might be a bug in impl)
  QTest::newRow("p_no_attr") << "<p>text</p>" << true; // No space after tag name
}

void TestHtmlUtils::testHasOnlyImgTag() {
  QFETCH(QString, html);
  QFETCH(bool, expected);

  QCOMPARE(HtmlUtils::hasOnlyImgTag(html), expected);
}

} // namespace tests

QTEST_GUILESS_MAIN(tests::TestHtmlUtils)
#include "test_htmlutils.moc"
