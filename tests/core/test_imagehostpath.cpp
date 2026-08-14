#include <QtTest>

#include <imagehost/imagehostpath.h>

using namespace vnotex;

namespace tests {

class TestImageHostPath : public QObject {
  Q_OBJECT

private slots:
  void testRemotePath_data();
  void testRemotePath();

  // Regression: the folder must be the note's OWN folder, not its parent.
  void testRemotePathUsesNoteFolderNotGrandparent();

  void testIsRemoteUrl_data();
  void testIsRemoteUrl();

  void testRemoteUrlIdentity_data();
  void testRemoteUrlIdentity();

  // The destructive decision: which remote images may be deleted at the host.
  void testRemoteUrlsToDeleteDropsStillReferencedUrl();
  void testRemoteUrlsToDeleteDeletesRemovedUrl();
  void testRemoteUrlsToDeleteIgnoresLocalPaths();
  void testRemoteUrlsToDeleteKeepsEquivalentSpelling_data();
  void testRemoteUrlsToDeleteKeepsEquivalentSpelling();
  void testRemoteUrlsToDeleteKeepsUnparsedMarkdownForms_data();
  void testRemoteUrlsToDeleteKeepsUnparsedMarkdownForms();
  void testRemoteUrlsToDeleteIsSortedAndDeduplicated();

  // The setting gate lives inside the helper, so it is covered here.
  void testRemoteUrlsToDeleteDisabledDeletesNothing();
  void testRemoteUrlsToDeleteKeepsEquivalentSpellingInUnparsedForm_data();
  void testRemoteUrlsToDeleteKeepsEquivalentSpellingInUnparsedForm();
  void testRemoteUrlsToDeleteKeepsAwkwardlyDelimitedUrl_data();
  void testRemoteUrlsToDeleteKeepsAwkwardlyDelimitedUrl();
  void testRemoteUrlsToDeleteIgnoresUnrelatedRemoteUrlsInContent();
};

void TestImageHostPath::testRemotePath_data() {
  QTest::addColumn<QString>("contentDirPath");
  QTest::addColumn<QString>("destFileName");
  QTest::addColumn<QString>("expected");

  QTest::newRow("plain") << "C:/notes/test_notebook-v4"
                         << "a177.png"
                         << "test_notebook-v4/a177.png";
  QTest::newRow("subfolder") << "C:/notes/test_notebook-v4/docs"
                             << "a177.png"
                             << "docs/a177.png";
  QTest::newRow("trailing slash") << "C:/notes/test_notebook-v4/"
                                  << "a177.png"
                                  << "test_notebook-v4/a177.png";
  QTest::newRow("unclean path") << "C:/notes/other/../test_notebook-v4"
                                << "a177.png"
                                << "test_notebook-v4/a177.png";
  QTest::newRow("folder with spaces") << "C:/notes/my notes"
                                      << "a177.png"
                                      << "my notes/a177.png";
  QTest::newRow("posix") << "/home/u/notebook"
                         << "a177.png"
                         << "notebook/a177.png";
  // No usable folder name -> bare file name.
  QTest::newRow("empty content path") << QString() << "a177.png" << "a177.png";
  QTest::newRow("windows root") << "C:/"
                                << "a177.png"
                                << "a177.png";
  QTest::newRow("windows root no slash") << "C:"
                                         << "a177.png"
                                         << "a177.png";
  QTest::newRow("windows root backslash") << "C:\\"
                                          << "a177.png"
                                          << "a177.png";
  QTest::newRow("unc share root") << "//server/share"
                                  << "a177.png"
                                  << "a177.png";
  QTest::newRow("unc folder") << "//server/share/nb"
                              << "a177.png"
                              << "nb/a177.png";
  QTest::newRow("posix root") << "/"
                              << "a177.png"
                              << "a177.png";
  // Nothing to upload.
  QTest::newRow("empty file name") << "C:/notes/nb" << QString() << QString();
}

void TestImageHostPath::testRemotePath() {
  QFETCH(QString, contentDirPath);
  QFETCH(QString, destFileName);
  QFETCH(QString, expected);

  QCOMPARE(ImageHostPath::remotePath(contentDirPath, destFileName), expected);
}

void TestImageHostPath::testRemotePathUsesNoteFolderNotGrandparent() {
  // MarkdownEditor::setContentPath() receives the note's parent DIRECTORY.
  // Building the remote path used to call QFileInfo::dir() on it, climbing one
  // level too far and uploading into "OneDrive - Microsoft/" instead of the
  // notebook folder.
  const QString contentDir = QStringLiteral("C:/Users/tanle/OneDrive - Microsoft/test_notebook-v4");
  const auto path = ImageHostPath::remotePath(contentDir, QStringLiteral("3d3e.png"));

  QCOMPARE(path, QStringLiteral("test_notebook-v4/3d3e.png"));
  QVERIFY(!path.contains(QStringLiteral("OneDrive")));
}

void TestImageHostPath::testIsRemoteUrl_data() {
  QTest::addColumn<QString>("url");
  QTest::addColumn<bool>("expected");

  QTest::newRow("https") << "https://gitee.com/u/repo/raw/master/nb/a.png" << true;
  QTest::newRow("http") << "http://example.com/a.png" << true;
  QTest::newRow("uppercase scheme") << "HTTPS://example.com/a.png" << true;
  QTest::newRow("relative") << "images/a.png" << false;
  QTest::newRow("parent relative") << "../images/a.png" << false;
  QTest::newRow("windows absolute") << "C:/notes/nb/images/a.png" << false;
  QTest::newRow("posix absolute") << "/home/u/nb/a.png" << false;
  QTest::newRow("empty") << QString() << false;
}

void TestImageHostPath::testIsRemoteUrl() {
  QFETCH(QString, url);
  QFETCH(bool, expected);

  QCOMPARE(ImageHostPath::isRemoteUrl(url), expected);
}

void TestImageHostPath::testRemoteUrlIdentity_data() {
  QTest::addColumn<QString>("a");
  QTest::addColumn<QString>("b");
  QTest::addColumn<bool>("same");

  QTest::newRow("identical") << "https://h/nb/a.png"
                             << "https://h/nb/a.png" << true;
  QTest::newRow("scheme case") << "https://h/nb/a.png"
                               << "HTTPS://h/nb/a.png" << true;
  QTest::newRow("host case") << "https://Gitee.com/nb/a.png"
                             << "https://gitee.com/nb/a.png" << true;
  QTest::newRow("query dropped") << "https://h/nb/a.png"
                                 << "https://h/nb/a.png?v=2" << true;
  QTest::newRow("fragment dropped") << "https://h/nb/a.png"
                                    << "https://h/nb/a.png#x" << true;
  QTest::newRow("percent encoded space") << "https://h/My%20Nb/a.png"
                                         << "https://h/My Nb/a.png" << true;
  QTest::newRow("dot segments") << "https://h/nb/a.png"
                                << "https://h/nb/./a.png" << true;
  QTest::newRow("https default port") << "https://h/nb/a.png"
                                      << "https://h:443/nb/a.png" << true;
  QTest::newRow("http default port") << "http://h/nb/a.png"
                                     << "http://h:80/nb/a.png" << true;
  QTest::newRow("non default port differs") << "https://h/nb/a.png"
                                            << "https://h:8443/nb/a.png" << false;
  // An encoded delimiter is NOT the same object as a real path separator.
  QTest::newRow("encoded slash differs") << "https://h/nb/a%2Fb.png"
                                         << "https://h/nb/a/b.png" << false;
  // Path case IS significant: hosts serve case-sensitive object paths.
  QTest::newRow("path case differs") << "https://h/nb/a.png"
                                     << "https://h/nb/A.png" << false;
  QTest::newRow("different file") << "https://h/nb/a.png"
                                  << "https://h/nb/b.png" << false;
  QTest::newRow("different host") << "https://h/nb/a.png"
                                  << "https://other/nb/a.png" << false;
}

void TestImageHostPath::testRemoteUrlIdentity() {
  QFETCH(QString, a);
  QFETCH(QString, b);
  QFETCH(bool, same);

  const auto idA = ImageHostPath::remoteUrlIdentity(a);
  QVERIFY(!idA.isEmpty());
  QCOMPARE(idA == ImageHostPath::remoteUrlIdentity(b), same);

  // A local path has no remote identity.
  QVERIFY(ImageHostPath::remoteUrlIdentity(QStringLiteral("images/a.png")).isEmpty());
}

void TestImageHostPath::testRemoteUrlsToDeleteDropsStillReferencedUrl() {
  // The reported bug: an image uploaded this session is in the candidate set
  // (it is tracked in m_insertedImages) but is still referenced by the note.
  const QString url = QStringLiteral("https://gitee.com/u/r/raw/master/nb/3d3e.png");
  const QString content = QStringLiteral("text\n![img](%1)\n").arg(url);

  QVERIFY(ImageHostPath::remoteUrlsToDelete(true, {url}, {url}, content).isEmpty());
}

void TestImageHostPath::testRemoteUrlsToDeleteDeletesRemovedUrl() {
  // The genuine cleanup case must still work: the link was removed from the
  // note, so nothing references the remote object any more.
  const QString url = QStringLiteral("https://gitee.com/u/r/raw/master/nb/3d3e.png");
  const QString content = QStringLiteral("text without any image\n");

  QCOMPARE(ImageHostPath::remoteUrlsToDelete(true, {url}, {}, content), QStringList{url});
}

void TestImageHostPath::testRemoteUrlsToDeleteIgnoresLocalPaths() {
  // Local assets are deleted through Buffer2::deleteAsset(), never here.
  const QSet<QString> candidates{QStringLiteral("images/a.png"), QStringLiteral("../out/b.png"),
                                 QStringLiteral("C:/notes/nb/c.png"),
                                 QStringLiteral("/home/u/d.png"), QStringLiteral("ftp://h/e.png")};

  QVERIFY(ImageHostPath::remoteUrlsToDelete(true, candidates, {}, QString()).isEmpty());
}

void TestImageHostPath::testRemoteUrlsToDeleteKeepsEquivalentSpelling_data() {
  QTest::addColumn<QString>("candidate");
  QTest::addColumn<QString>("currentSpelling");

  QTest::newRow("scheme case") << "https://h/nb/a.png"
                               << "HTTPS://h/nb/a.png";
  QTest::newRow("host case") << "https://gitee.com/nb/a.png"
                             << "https://Gitee.com/nb/a.png";
  QTest::newRow("query added") << "https://h/nb/a.png"
                               << "https://h/nb/a.png?cache=2";
  QTest::newRow("fragment added") << "https://h/nb/a.png"
                                  << "https://h/nb/a.png#frag";
  QTest::newRow("encoded space") << "https://h/My%20Nb/a.png"
                                 << "https://h/My Nb/a.png";
}

void TestImageHostPath::testRemoteUrlsToDeleteKeepsEquivalentSpelling() {
  QFETCH(QString, candidate);
  QFETCH(QString, currentSpelling);

  // The note now spells the URL differently, but it is the SAME remote object,
  // so deleting it would break the note.
  const QString content = QStringLiteral("![img](%1)").arg(currentSpelling);

  QVERIFY(
      ImageHostPath::remoteUrlsToDelete(true, {candidate}, {currentSpelling}, content).isEmpty());
}

void TestImageHostPath::testRemoteUrlsToDeleteKeepsUnparsedMarkdownForms_data() {
  QTest::addColumn<QString>("content");

  const QString url = QStringLiteral("https://gitee.com/u/r/raw/master/nb/3d3e.png");
  QTest::newRow("angle bracket destination") << QStringLiteral("![img](<%1>)").arg(url);
  QTest::newRow("reference style") << QStringLiteral("![img][ref]\n\n[ref]: %1\n").arg(url);
  QTest::newRow("html img tag") << QStringLiteral("<img src=\"%1\" />").arg(url);
}

void TestImageHostPath::testRemoteUrlsToDeleteKeepsUnparsedMarkdownForms() {
  QFETCH(QString, content);

  // These forms are NOT returned by the markdown image scan, so currentUrls is
  // empty. The raw-content backstop must still prevent the deletion.
  const QString url = QStringLiteral("https://gitee.com/u/r/raw/master/nb/3d3e.png");

  QVERIFY(ImageHostPath::remoteUrlsToDelete(true, {url}, {}, content).isEmpty());
}

void TestImageHostPath::testRemoteUrlsToDeleteIsSortedAndDeduplicated() {
  const QString a = QStringLiteral("https://h/nb/a.png");
  const QString b = QStringLiteral("https://h/nb/b.png");
  const QString kept = QStringLiteral("https://h/nb/kept.png");
  const QString content = QStringLiteral("![img](%1)").arg(kept);

  const auto result = ImageHostPath::remoteUrlsToDelete(true, {b, a, kept}, {kept}, content);

  QCOMPARE(result, QStringList({a, b}));
}

void TestImageHostPath::testRemoteUrlsToDeleteDisabledDeletesNothing() {
  // Settings -> Image Host -> "Clear obsolete images" is off. Nothing may be
  // deleted at the host, however obsolete the candidate looks.
  const QString url = QStringLiteral("https://gitee.com/u/r/raw/master/nb/3d3e.png");

  QVERIFY(ImageHostPath::remoteUrlsToDelete(false, {url}, {}, QStringLiteral("no image here"))
              .isEmpty());
  // Sanity: the very same input IS deletable once the setting is on, so the
  // assertion above is really testing the gate.
  QCOMPARE(ImageHostPath::remoteUrlsToDelete(true, {url}, {}, QStringLiteral("no image here")),
           QStringList{url});
}

void TestImageHostPath::testRemoteUrlsToDeleteKeepsEquivalentSpellingInUnparsedForm_data() {
  QTest::addColumn<QString>("content");

  // The note references the SAME object, but (a) in a form the markdown image
  // scan does not report, and (b) with a different spelling, so neither the
  // parsed-URL set nor a literal text match can see it.
  QTest::newRow("reference definition, scheme+host case")
      << "![img][ref]\n\n[ref]: HTTPS://GITEE.COM/u/r/raw/master/nb/3d3e.png\n";
  QTest::newRow("angle bracket, query added")
      << "![img](<https://gitee.com/u/r/raw/master/nb/3d3e.png?cache=2>)";
  QTest::newRow("html img, fragment added")
      << "<img src=\"https://gitee.com/u/r/raw/master/nb/3d3e.png#x\" />";
  QTest::newRow("html img, default port")
      << "<img src=\"https://gitee.com:443/u/r/raw/master/nb/3d3e.png\" />";
}

void TestImageHostPath::testRemoteUrlsToDeleteKeepsAwkwardlyDelimitedUrl_data() {
  QTest::addColumn<QString>("candidate");
  QTest::addColumn<QString>("content");

  // The raw-URL scan cannot delimit these exactly (the path itself contains a
  // closing delimiter / trailing punctuation / an HTML entity / a fold), and
  // the note spells the URL differently, so neither the literal check nor the
  // identity check can see it. Deletion must still be refused.
  QTest::newRow("parenthesis in path")
      << "https://gitee.com/u/r/raw/master/nb/x(1).png"
      << "![img][ref]\n\n[ref]: HTTPS://GITEE.COM/u/r/raw/master/nb/x(1).png\n";
  QTest::newRow("bracket in path")
      << "https://gitee.com/u/r/raw/master/nb/x[1].png"
      << "<img src=\"HTTPS://gitee.com/u/r/raw/master/nb/x[1].png\" />";
  QTest::newRow("trailing period in path")
      << "https://gitee.com/u/r/raw/master/nb/x."
      << "![img][ref]\n\n[ref]: HTTPS://GITEE.COM/u/r/raw/master/nb/x.\n";
  QTest::newRow("html entity in path")
      << "https://gitee.com/u/r/raw/master/nb/a&b.png"
      << "<img src=\"HTTPS://gitee.com/u/r/raw/master/nb/a&amp;b.png\" />";
  QTest::newRow("folded html attribute")
      << "https://gitee.com/u/r/raw/master/nb/3d3e.png"
      << "<img\n  src=\"HTTPS://gitee.com/u/r/raw/master/nb/\n  3d3e.png\" />";
  QTest::newRow("encoded space in candidate")
      << "https://gitee.com/u/r/raw/master/My%20Nb/a.png"
      << "![img][ref]\n\n[ref]: HTTPS://GITEE.COM/u/r/raw/master/My Nb/a.png\n";
  QTest::newRow("numeric html entity in path")
      << "https://gitee.com/u/r/raw/master/nb/a&b.png"
      << "<img src=\"HTTPS://GITEE.COM/u/r/raw/master/nb/a&#38;b.png\">";
  QTest::newRow("hex html entity in path")
      << "https://gitee.com/u/r/raw/master/nb/a&b.png"
      << "<img src=\"HTTPS://GITEE.COM/u/r/raw/master/nb/a&#x26;b.png\">";
  QTest::newRow("folded percent encoding")
      << "https://gitee.com/u/r/raw/master/My%20Nb/a.png"
      << "<img src=\"HTTPS://GITEE.COM/u/r/raw/master/My%\n20Nb/a.png\">";
}

void TestImageHostPath::testRemoteUrlsToDeleteKeepsAwkwardlyDelimitedUrl() {
  QFETCH(QString, candidate);
  QFETCH(QString, content);

  QVERIFY(ImageHostPath::remoteUrlsToDelete(true, {candidate}, {}, content).isEmpty());
}

void TestImageHostPath::testRemoteUrlsToDeleteKeepsEquivalentSpellingInUnparsedForm() {
  QFETCH(QString, content);

  const QString candidate = QStringLiteral("https://gitee.com/u/r/raw/master/nb/3d3e.png");

  QVERIFY(ImageHostPath::remoteUrlsToDelete(true, {candidate}, {}, content).isEmpty());
}

void TestImageHostPath::testRemoteUrlsToDeleteIgnoresUnrelatedRemoteUrlsInContent() {
  // Remote URLs in the content that denote OTHER objects must not keep an
  // obsolete candidate alive, otherwise the feature never deletes anything.
  const QString candidate = QStringLiteral("https://gitee.com/u/r/raw/master/nb/3d3e.png");
  const QString content = QStringLiteral(
      "![other](https://gitee.com/u/r/raw/master/nb/other.png)\n"
      "see https://example.com/page.html and https://gitee.com/u/r/raw/master/other-nb/3d3e.png\n");

  QCOMPARE(ImageHostPath::remoteUrlsToDelete(true, {candidate}, {}, content),
           QStringList{candidate});
}

} // namespace tests

QTEST_MAIN(tests::TestImageHostPath)

#include "test_imagehostpath.moc"
