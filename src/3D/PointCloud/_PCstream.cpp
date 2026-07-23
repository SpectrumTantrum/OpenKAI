/*
 * _PCstream.cpp
 *
 *  Created on: May 24, 2020
 *      Author: yankai
 */

#include "_PCstream.h"

namespace kai
{

    _PCstream::_PCstream()
    {
        m_type = pc_stream;

        m_pP = nullptr;
        m_nP = 10000;
        m_iP = 0;
        m_nValid = 0;
        m_sequence = 0;
        m_pSnapshot = nullptr;
    }

    _PCstream::~_PCstream()
    {
        m_iP = 0;
        m_nValid = 0;
        m_nP = 0;
        DEL_ARRAY(m_pP);
    }

    bool _PCstream::init(const json &j)
    {
        IF_F(!this->_GeometryBase::init(j));

        jKv(j, "nP", m_nP);
        IF_Le_F(m_nP <= 0, "Invalid nP: " + i2str(m_nP));

        return initGeometry();
    }

    bool _PCstream::initGeometry(void)
    {
        mutexLock();

        DEL_ARRAY(m_pP);
        m_pP = new GEOMETRY_POINT[m_nP];
        if (!m_pP)
        {
            mutexUnlock();
            return false;
        }
        m_iP = 0;
        m_nValid = 0;

        for (int i = 0; i < m_nP; i++)
            m_pP[i].clear();

        invalidateSnapshot();
        mutexUnlock();

        return true;
    }

    void _PCstream::clear(void)
    {
        mutexLock();

        for (int i = 0; i < m_nP; i++)
            m_pP[i].clear();

        m_iP = 0;
        m_nValid = 0;

        invalidateSnapshot();
        mutexUnlock();
    }

    bool _PCstream::start(void)
    {
        NULL_F(m_pT);
        return m_pT->startThread(getUpdate, this);
    }

    bool _PCstream::check(void)
    {
        NULL_F(m_pP);

        return this->_GeometryBase::check();
    }

    void _PCstream::update(void)
    {
        while (m_pT->bAlive())
        {
            m_pT->autoFPS();

            updatePCstream();
        }
    }

    void _PCstream::updatePCstream(void)
    {
        IF_(!check());

        if (m_pSM)
        {
            readSharedMem();
            writeSharedMem();
        }
    }

    void _PCstream::addPCstream(void *p, uint64_t tExpire)
    {
        IF_(!check());
        NULL_(p);
        _PCstream *pS = (_PCstream *)p;

        PCSTREAM_SNAPSHOT_PTR pSnapshot = pS->getSnapshot();
        NULL_(pSnapshot);
        uint64_t tNow = getApproxTbootUs();
        vector<GEOMETRY_POINT> vP;
        vP.reserve(pSnapshot->m_vP.size());

        for (const GEOMETRY_POINT &p : pSnapshot->m_vP)
        {
            IF_CONT(tExpire > 0 && p.m_tStamp != 0 && bExpired(p.m_tStamp, tExpire, tNow));

            vP.push_back(p);
        }

        addBatch(vP);
    }

    void _PCstream::writeSharedMem(void)
    {
        NULL_(m_pSM);
        IF_(!m_pSM->bOpen());
        IF_(!m_pSM->bWriter());

        int nPw = small<int>(m_nP, m_pSM->nB() / sizeof(GEOMETRY_POINT));

        mutexLock();
        memcpy(m_pSM->p(), m_pP, nPw * sizeof(GEOMETRY_POINT));
        mutexUnlock();
    }

    void _PCstream::readSharedMem(void)
    {
        NULL_(m_pSM);
        IF_(!m_pSM->bOpen());
        IF_(m_pSM->bWriter());

        //		memcpy(m_pP, m_pSM->p(), m_nP * sizeof(GEOMETRY_POINT));
        vector<GEOMETRY_POINT> vP;
        vP.reserve(m_nP);
        GEOMETRY_POINT *pSM = (GEOMETRY_POINT *)m_pSM->p();
        for (int i = 0; i < m_nP; i++)
        {
            GEOMETRY_POINT p = pSM[i];
            Vector3d eV = m_A * v2e(p.m_vP).cast<double>();
            p.m_vP = e2v((Vector3f)eV.cast<float>());

            if (m_bColOverwrite)
            {
                float d = p.m_vP.len();
                p.m_vC.set(
                    1.0 - (m_vkColR.constrain(d) - m_vkColR.x) * m_vkColOv.x,
                    (m_vkColG.constrain(d) - m_vkColG.x) * m_vkColOv.y,
                    (m_vkColB.constrain(d) - m_vkColB.x) * m_vkColOv.z);
            }

            vP.push_back(p);
        }

        addBatch(vP);
    }

    void _PCstream::copyTo(PointCloud *pPC, const uint64_t tExpire)
    {
        IF_(!check());
        NULL_(pPC);

        PCSTREAM_SNAPSHOT_PTR pSnapshot = getSnapshot();
        NULL_(pSnapshot);

        for (const GEOMETRY_POINT &p : pSnapshot->m_vP)
        {
            IF_CONT(p.m_tStamp < tExpire);

            pPC->points_.push_back(v2e(p.m_vP).cast<double>());
            pPC->colors_.push_back(v2e(p.m_vC).cast<double>());
        }
    }

    void _PCstream::add(const Vector3d &vP, const Vector3f &vC, uint64_t tStamp)
    {
        add(e2v((Vector3f)vP.cast<float>()),
            e2v((Vector3f)vC.cast<float>()),
            tStamp);
    }

    void _PCstream::add(const vFloat3 &vP, const vFloat3 &vC, uint64_t tStamp)
    {
        mutexLock();

        GEOMETRY_POINT *pP = &m_pP[m_iP];
        pP->m_vP = vP;
        pP->m_vC = vC;
        pP->m_tStamp = tStamp;

        m_iP = iRing(m_iP, m_nP);
        if (m_nValid < m_nP)
            ++m_nValid;
        invalidateSnapshot();

        mutexUnlock();
    }

    void _PCstream::addBatch(const vector<GEOMETRY_POINT> &vP)
    {
        IF_(vP.empty());
        NULL_(m_pP);

        mutexLock();

        for (const GEOMETRY_POINT &p : vP)
        {
            m_pP[m_iP] = p;
            m_iP = iRing(m_iP, m_nP);
            if (m_nValid < m_nP)
                ++m_nValid;
        }

        invalidateSnapshot();
        mutexUnlock();
    }

    PCSTREAM_SNAPSHOT_PTR _PCstream::getSnapshot(void)
    {
        NULL_N(m_pP);

        mutexLock();
        if (!m_pSnapshot)
            m_pSnapshot = buildSnapshot();
        PCSTREAM_SNAPSHOT_PTR pSnapshot = m_pSnapshot;
        mutexUnlock();

        return pSnapshot;
    }

    void _PCstream::copyRingTo(vector<GEOMETRY_POINT> *pRing)
    {
        NULL_(pRing);

        mutexLock();
        if (m_pP && m_nP > 0)
            pRing->assign(m_pP, m_pP + m_nP);
        else
            pRing->clear();
        mutexUnlock();
    }

    void _PCstream::invalidateSnapshot(void)
    {
        ++m_sequence;
        m_pSnapshot.reset();
    }

    PCSTREAM_SNAPSHOT_PTR _PCstream::buildSnapshot(void)
    {
        shared_ptr<PCSTREAM_SNAPSHOT> pSnapshot = make_shared<PCSTREAM_SNAPSHOT>();
        pSnapshot->m_sequence = m_sequence;
        pSnapshot->m_vP.reserve(m_nValid);

        for (int i = 0; i < m_nValid; ++i)
        {
            const GEOMETRY_POINT &p = m_pP[i];

            pSnapshot->m_vP.push_back(p);
            pSnapshot->m_tStamp = big(pSnapshot->m_tStamp, p.m_tStamp);
        }

        return pSnapshot;
    }

    GEOMETRY_POINT *_PCstream::get(int i)
    {
        IF__(i < 0 || i >= m_nP, nullptr);

        return &m_pP[i];
    }

    int _PCstream::nP(void)
    {
        return m_nP;
    }

    int _PCstream::iP(void)
    {
        mutexLock();
        int iP = m_iP;
        mutexUnlock();

        return iP;
    }

    bool _PCstream::saveFile(const string &fName)
    {
        IF_F(fName.empty());

        PointCloud pc;
        this->copyTo(&pc);

        io::WritePointCloudOption par;
        par.write_ascii = io::WritePointCloudOption::IsAscii::Binary;
        par.compressed = io::WritePointCloudOption::Compressed::Uncompressed;

        return io::WritePointCloudToPLY(fName.c_str(), pc, par);
    }

    void _PCstream::console(void *pConsole)
    {
        NULL_(pConsole);
        this->_GeometryBase::console(pConsole);
    }

    void _PCstream::console(const json &j, void *pJSONbase)
    {
        _JSONbase *pJb = (_JSONbase *)pJSONbase;
        string cmd;
        IF_(!jKv(j, "cmd", cmd));

        if (cmd == "savePly")
        {
            string fPly;
            IF_(!jKv(j, "fNamePly", fPly));

            bool bR = saveFile(fPly);

            NULL_(pJb);
            json jr = json::object();
            jr["cmd"] = "savePly";
            jr["bSuccess"] = bR;
            pJb->sendJson(jr);
        }
    }

}
