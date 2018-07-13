#include "pegparser.h"

enum WorkerState
{
    Idle,
    Busy,
    Cancelled,
    Finished
};

void PegParseResult::parse(QAtomicInt &p_stop)
{
    parseImageRegions(p_stop);

    parseHeaderRegions(p_stop);

    parseFencedCodeBlockRegions(p_stop);

    parseInlineEquationRegions(p_stop);

    parseDisplayFormulaRegions(p_stop);
}

void PegParseResult::parseImageRegions(QAtomicInt &p_stop)
{
    // From Qt5.7, the capacity is preserved.
    m_imageRegions.clear();
    if (isEmpty()) {
        return;
    }

    pmh_element *elem = m_pmhElements[pmh_IMAGE];
    while (elem != NULL) {
        if (elem->end <= elem->pos) {
            elem = elem->next;
            continue;
        }

        if (p_stop.load() == 1) {
            return;
        }

        m_imageRegions.push_back(VElementRegion(elem->pos, elem->end));
        elem = elem->next;
    }
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

            m_headerRegions.push_back(VElementRegion(elem->pos, elem->end));
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

        if (!m_codeBlockRegions.contains(elem->pos)) {
            m_codeBlockRegions.insert(elem->pos, VElementRegion(elem->pos, elem->end));
        }

        elem = elem->next;
    }
}

void PegParseResult::parseInlineEquationRegions(QAtomicInt &p_stop)
{
    m_inlineEquationRegions.clear();
    if (isEmpty()) {
        return;
    }

    pmh_element *elem = m_pmhElements[pmh_INLINEEQUATION];
    while (elem != NULL) {
        if (elem->end <= elem->pos) {
            elem = elem->next;
            continue;
        }

        if (p_stop.load() == 1) {
            return;
        }

        m_inlineEquationRegions.push_back(VElementRegion(elem->pos, elem->end));
        elem = elem->next;
    }
}

void PegParseResult::parseDisplayFormulaRegions(QAtomicInt &p_stop)
{
    m_displayFormulaRegions.clear();
    if (isEmpty()) {
        return;
    }

    pmh_element *elem = m_pmhElements[pmh_DISPLAYFORMULA];
    while (elem != NULL) {
        if (elem->end <= elem->pos) {
            elem = elem->next;
            continue;
        }

        if (p_stop.load() == 1) {
            return;
        }

        m_displayFormulaRegions.push_back(VElementRegion(elem->pos, elem->end));
        elem = elem->next;
    }

    if (p_stop.load() == 1) {
        return;
    }

    std::sort(m_displayFormulaRegions.begin(), m_displayFormulaRegions.end());
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

    result->parse(p_stop);

    return result;
}


#define NUM_OF_THREADS 2

PegParser::PegParser(QObject *p_parent)
    : QObject(p_parent)
{
    init();
}

void PegParser::parseAsync(const QSharedPointer<PegParseConfig> &p_config)
{
    m_pendingWork = p_config;

    pickWorker();
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

    pmh_element *elem = res[pmh_IMAGE];
    while (elem != NULL) {
        if (elem->end <= elem->pos) {
            elem = elem->next;
            continue;
        }

        regs.push_back(VElementRegion(elem->pos, elem->end));
        elem = elem->next;
    }

    pmh_free_elements(res);

    return regs;
}

pmh_element **PegParser::parseMarkdownToElements(const QSharedPointer<PegParseConfig> &p_config)
{
    if (p_config->m_data.isEmpty()) {
        return NULL;
    }

    pmh_element **pmhResult = NULL;
    char *data = p_config->m_data.data();
    pmh_markdown_to_elements(data, p_config->m_extensions, &pmhResult);
    return pmhResult;
}
