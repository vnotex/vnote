#include "pegparser.h"

enum WorkerState
{
    Idle,
    Busy,
    Cancelled,
    Finished
};

void PegParseResult::parse(QAtomicInt &p_stop, bool p_fast)
{
    if (p_fast) {
        return;
    }

    parseImageRegions(p_stop);

    parseHeaderRegions(p_stop);

    parseFencedCodeBlockRegions(p_stop);

    parseInlineEquationRegions(p_stop);

    parseDisplayFormulaRegions(p_stop);

    parseHRuleRegions(p_stop);

    parseTableRegions(p_stop);

    parseTableHeaderRegions(p_stop);

    parseTableBorderRegions(p_stop);
}

void PegParseResult::parseImageRegions(QAtomicInt &p_stop)
{
    parseRegions(p_stop,
                 pmh_IMAGE,
                 m_imageRegions,
                 false);
}

void PegParseResult::parseHeaderRegions(QAtomicInt &p_stop)
{
    // From Qt5.7, the capacity is preserved.
    m_headerRegions.clear();
    if (isEmpty()) {
        return;
    }

    pmh_element_type hx[6] = {pmh_H1, pmh_H2, pmh_H3, pmh_H4, pmh_H5, pmh_H6};
    for (int i = 0; i < 6; ++i) {
        pmh_element *elem = m_pmhElements[hx[i]];
        while (elem != NULL) {
            if (elem->end <= elem->pos) {
                elem = elem->next;
                continue;
            }

            if (p_stop.load() == 1) {
                return;
            }

            m_headerRegions.push_back(VElementRegion(m_offset + elem->pos, m_offset + elem->end));
            elem = elem->next;
        }
    }

    if (p_stop.load() == 1) {
        return;
    }

    std::sort(m_headerRegions.begin(), m_headerRegions.end());
}

void PegParseResult::parseFencedCodeBlockRegions(QAtomicInt &p_stop)
{
    m_codeBlockRegions.clear();
    if (isEmpty()) {
        return;
    }

    pmh_element *elem = m_pmhElements[pmh_FENCEDCODEBLOCK];
    while (elem != NULL) {
        if (elem->end <= elem->pos) {
            elem = elem->next;
            continue;
        }

        if (p_stop.load() == 1) {
            return;
        }

        if (!m_codeBlockRegions.contains(m_offset + elem->pos)) {
            m_codeBlockRegions.insert(m_offset + elem->pos,
                                      VElementRegion(m_offset + elem->pos, m_offset + elem->end));
        }

        elem = elem->next;
    }
}

void PegParseResult::parseInlineEquationRegions(QAtomicInt &p_stop)
{
    parseRegions(p_stop,
                 pmh_INLINEEQUATION,
                 m_inlineEquationRegions,
                 false);
}

void PegParseResult::parseDisplayFormulaRegions(QAtomicInt &p_stop)
{
    parseRegions(p_stop,
                 pmh_DISPLAYFORMULA,
                 m_displayFormulaRegions,
                 true);
}

void PegParseResult::parseHRuleRegions(QAtomicInt &p_stop)
{
    parseRegions(p_stop,
                 pmh_HRULE,
                 m_hruleRegions,
                 false);
}

void PegParseResult::parseTableRegions(QAtomicInt &p_stop)
{
    parseRegions(p_stop,
                 pmh_TABLE,
                 m_tableRegions,
                 true);
}

void PegParseResult::parseTableHeaderRegions(QAtomicInt &p_stop)
{
    parseRegions(p_stop,
                 pmh_TABLEHEADER,
                 m_tableHeaderRegions,
                 true);
}

void PegParseResult::parseTableBorderRegions(QAtomicInt &p_stop)
{
    parseRegions(p_stop,
                 pmh_TABLEBORDER,
                 m_tableBorderRegions,
                 true);
}

void PegParseResult::parseRegions(QAtomicInt &p_stop,
                                  pmh_element_type p_type,
                                  QVector<VElementRegion> &p_result,
                                  bool p_sort)
{
    p_result.clear();
    if (isEmpty()) {
        return;
    }

    pmh_element *elem = m_pmhElements[p_type];
    while (elem != NULL) {
        if (elem->end <= elem->pos) {
            elem = elem->next;
            continue;
        }

        if (p_stop.load() == 1) {
            return;
        }

        p_result.push_back(VElementRegion(m_offset + elem->pos, m_offset + elem->end));
        elem = elem->next;
    }

    if (p_sort && p_stop.load() != 1) {
        std::sort(p_result.begin(), p_result.end());
    }
}


PegParserWorker::PegParserWorker(QObject *p_parent)
    : QThread(p_parent),
      m_stop(0),
      m_state(WorkerState::Idle)
{
}

void PegParserWorker::prepareParse(const QSharedPointer<PegParseConfig> &p_config)
{
    Q_ASSERT(m_parseConfig.isNull());

    m_state = WorkerState::Busy;
    m_parseConfig = p_config;
}

void PegParserWorker::reset()
{
    m_parseConfig.reset();
    m_parseResult.reset();
    m_stop.store(0);
    m_state = WorkerState::Idle;
}

void PegParserWorker::stop()
{
    m_stop.store(1);
}

void PegParserWorker::run()
{
    Q_ASSERT(m_state == WorkerState::Busy);

    m_parseResult = parseMarkdown(m_parseConfig, m_stop);

    if (isAskedToStop()) {
        m_state = WorkerState::Cancelled;
        return;
    }

    m_state = WorkerState::Finished;
}

QSharedPointer<PegParseResult> PegParserWorker::parseMarkdown(const QSharedPointer<PegParseConfig> &p_config,
                                                              QAtomicInt &p_stop)
{
    QSharedPointer<PegParseResult> result(new PegParseResult(p_config));

    if (p_config->m_data.isEmpty()) {
        return result;
    }

    result->m_pmhElements = PegParser::parseMarkdownToElements(p_config);

    if (p_stop.load() == 1) {
        return result;
    }

    result->parse(p_stop, p_config->m_fast);

    return result;
}


#define NUM_OF_THREADS 2

PegParser::PegParser(QObject *p_parent)
    : QObject(p_parent)
{
    init();
}

void PegParser::init()
{
    for (int i = 0; i < NUM_OF_THREADS; ++i) {
        PegParserWorker *th = new PegParserWorker(this);
        connect(th, &PegParserWorker::finished,
                this, [this, th]() {
                    handleWorkerFinished(th);
                });

        m_workers.append(th);
    }
}

void PegParser::clear()
{
    m_pendingWork.reset();

    for (auto const & th : m_workers) {
        th->quit();
        th->wait();

        delete th;
    }

    m_workers.clear();
}

PegParser::~PegParser()
{
    clear();
}

void PegParser::parseAsync(const QSharedPointer<PegParseConfig> &p_config)
{
    m_pendingWork = p_config;

    pickWorker();
}

QSharedPointer<PegParseResult> PegParser::parse(const QSharedPointer<PegParseConfig> &p_config)
{
    QSharedPointer<PegParseResult> result(new PegParseResult(p_config));

    if (p_config->m_data.isEmpty()) {
        return result;
    }

    result->m_pmhElements = PegParser::parseMarkdownToElements(p_config);

    QAtomicInt stop(0);
    result->parse(stop, p_config->m_fast);

    return result;
}

void PegParser::handleWorkerFinished(PegParserWorker *p_worker)
{
    QSharedPointer<PegParseResult> result;
    if (p_worker->state() == WorkerState::Finished) {
        result = p_worker->parseResult();
    }

    p_worker->reset();

    pickWorker();

    if (!result.isNull()) {
        emit parseResultReady(result);
    }
}

void PegParser::pickWorker()
{
    if (m_pendingWork.isNull()) {
        return;
    }

    bool allBusy = true;
    for (auto th : m_workers) {
        if (th->state() == WorkerState::Idle) {
            scheduleWork(th, m_pendingWork);
            m_pendingWork.reset();
            return;
        } else if (th->state() != WorkerState::Busy) {
            allBusy = false;
        }
    }

    if (allBusy) {
        // Need to stop the worker with non-minimal timestamp.
        int idx = 0;
        TimeStamp minTS = m_workers[idx]->workTimeStamp();

        if (m_workers.size() > 1) {
            if (m_workers[1]->workTimeStamp() > minTS) {
                idx = 1;
            }
        }

        m_workers[idx]->stop();
    }
}

void PegParser::scheduleWork(PegParserWorker *p_worker,
                             const QSharedPointer<PegParseConfig> &p_config)
{
    Q_ASSERT(p_worker->state() == WorkerState::Idle);

    p_worker->reset();
    p_worker->prepareParse(p_config);
    p_worker->start();
}

QVector<VElementRegion> PegParser::parseImageRegions(const QSharedPointer<PegParseConfig> &p_config)
{
    QVector<VElementRegion> regs;
    pmh_element **res = PegParser::parseMarkdownToElements(p_config);
    if (!res) {
        return regs;
    }

    int offset = p_config->m_offset;
    pmh_element *elem = res[pmh_IMAGE];
    while (elem != NULL) {
        if (elem->end <= elem->pos) {
            elem = elem->next;
            continue;
        }

        regs.push_back(VElementRegion(offset + elem->pos, offset + elem->end));
        elem = elem->next;
    }

    pmh_free_elements(res);

    return regs;
}

#define MAX_CODE_POINT 65535

#define X_CHAR 86U

#define HAS_UTF8_BOM(x)         ( ((*x & 0xFF) == 0xEF)\
                                  && ((*(x+1) & 0xFF) == 0xBB)\
                                  && ((*(x+2) & 0xFF) == 0xBF) )

// Calculate the UTF8 code point.
// Return the number of chars consumed.
static inline int utf8CodePoint(const char *p_ch, int &p_codePoint)
{
    unsigned char uch = *p_ch;

    if ((uch & 0x80) == 0) {
        p_codePoint = uch;
        return 1;
    } else if ((uch & 0xE0) == 0xC0) {
        // 110yyyxx 10xxxxxx -> 00000yyy xxxxxxxx
        unsigned char uch2 = *(p_ch + 1);
        p_codePoint = ((uch & 0x1CL) << 6) + ((uch & 0x3L) << 6) + (uch2 & 0x3FL);
        return 2;
    } else if ((uch & 0xF0) == 0xE0) {
        // 1110yyyy 10yyyyxx 10xxxxxx -> yyyyyyyy xxxxxxxx
        unsigned char uch2 = *(p_ch + 1);
        unsigned char uch3 = *(p_ch + 2);
        p_codePoint = ((uch & 0xF) << 12)
                      + ((uch2 & 0x3CL) << 6) + ((uch2 & 0x3L) << 6)
                      + (uch3 & 0x3FL);
        return 3;
    } else if ((uch & 0xF8) == 0xF0) {
        // 11110zzz 10zzyyyy 10yyyyxx 10xxxxxx -> 000zzzzz yyyyyyyy xxxxxxxx
        unsigned char uch2 = *(p_ch + 1);
        unsigned char uch3 = *(p_ch + 2);
        unsigned char uch4 = *(p_ch + 3);
        p_codePoint = ((uch & 0x7L) << 18)
                      + ((uch2 & 0x30L) << 12) + ((uch2 & 0xFL) << 12)
                      + ((uch3 & 0x3CL) << 6) + ((uch3 & 0x3L) << 6)
                      + (uch4 & 0x3FL);
        return 4;
    } else {
        return -1;
    }
}

static inline void copyChars(char *p_dest, const char *p_src, int p_num)
{
    for (int i = 0; i < p_num; ++i) {
        *(p_dest + i) = *(p_src + i);
    }
}

// @p_data: UTF-8 data array.
// If @p_data contain unicode characters with code value above 65535, it will break
// it into two characters with code value below 65536.
// Return null if there is no fix. Otherwise, return a fixed copy of the data.
static QSharedPointer<char> tryFixUnicodeData(const char *p_data)
{
    bool needFix = false;
    int sz = 0;

    const char *ch = p_data;
    bool hasBOM = false;
    if (HAS_UTF8_BOM(ch)) {
        hasBOM = true;
        ch += 3;
        sz += 3;
    }

    // Calculate the size of fixed data.
    while (*ch != '\0') {
        int cp;
        int nr = utf8CodePoint(ch, cp);
        if (nr == -1) {
            return NULL;
        }

        if (cp > MAX_CODE_POINT) {
            needFix = true;
            ch += nr;
            // Use two one-byte chars to replace.
            sz += 2;
        } else {
            ch += nr;
            sz += nr;
        }
    }

    if (!needFix) {
        return NULL;
    }

    // Replace those chars with two one-byte chars.
    QSharedPointer<char> res(new char[sz + 1]);
    char *newChar = res.data();
    int idx = 0;
    ch = p_data;
    if (hasBOM) {
        copyChars(newChar + idx, ch, 3);
        ch += 3;
        idx += 3;
    }

    while (*ch != '\0') {
        int cp;
        int nr = utf8CodePoint(ch, cp);
        Q_ASSERT(nr > 0);
        if (cp > MAX_CODE_POINT) {
            *(newChar + idx) = X_CHAR;
            *(newChar + idx + 1) = X_CHAR;
            ch += nr;
            idx += 2;
        } else {
            copyChars(newChar + idx, ch, nr);
            ch += nr;
            idx += nr;
        }
    }

    Q_ASSERT(idx == sz);
    *(newChar + sz) = '\0';

    return res;
}

pmh_element **PegParser::parseMarkdownToElements(const QSharedPointer<PegParseConfig> &p_config)
{
    if (p_config->m_data.isEmpty()) {
        return NULL;
    }

    pmh_element **pmhResult = NULL;

    // p_config->m_data is encoding in UTF-8.
    // QString stores a string of 16-bit QChars. Unicode characters with code values above 65535 are stored using surrogate pairs, i.e., two consecutive QChars.
    // Hence, a QString using two QChars to save one code value if it's above 65535, with size()
    // returning 2. pmh_markdown_to_elements() will treat it at the size of 1 (expectively).
    // To make it work, we split unicode characters whose code value is above 65535 into two unicode
    // characters whose code value is below 65535.
    char *data = p_config->m_data.data();
    QSharedPointer<char> fixedData = tryFixUnicodeData(data);
    if (fixedData) {
        data = fixedData.data();
    }

    pmh_markdown_to_elements(data, p_config->m_extensions, &pmhResult);
    return pmhResult;
}
