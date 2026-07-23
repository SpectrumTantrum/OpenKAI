#include "O3DUI.h"

namespace open3d
{
    namespace visualization
    {
        namespace visualizer
        {
            namespace
            {
                template <typename T>
                shared_ptr<T> GiveOwnership(T *ptr)
                {
                    return shared_ptr<T>(ptr);
                }
            }

            O3DUI::O3DUI(const string &title, int width, int height)
                : Window(title, width, height), m_bClosing(false)
            {
            }

            O3DUI::~O3DUI()
            {
                beginClose();
            }

            void O3DUI::Init(void)
            {
                m_pScene = new SceneWidget();
                m_pScene->SetScene(make_shared<Open3DScene>(GetRenderer()));
                AddChild(GiveOwnership(m_pScene));

                SetOnTickEvent([this]()
                               { return flushPending(); });
                SetOnClose([this]()
                           {
                               beginClose();
                               return true;
                           });
            }

            void O3DUI::RequestClose(void)
            {
                queueAction("9.window.close", [this]()
                            { Close(); });
            }

            bool O3DUI::bClosing(void) const
            {
                return m_bClosing.load();
            }

            void O3DUI::beginClose(void)
            {
                if (m_bClosing.exchange(true))
                    return;

                lock_guard<mutex> lock(m_pendingMutex);
                m_pendingGeometry.clear();
                m_pendingActions.clear();
            }

            void O3DUI::queueGeometry(const string &name, PENDING_GEOMETRY &&geometry)
            {
                if (m_bClosing.load())
                    return;

                lock_guard<mutex> lock(m_pendingMutex);
                if (m_bClosing.load())
                    return;

                auto pending = m_pendingGeometry.find(name);
                if (!geometry.m_bAdd && pending != m_pendingGeometry.end() &&
                    pending->second.m_type == geometry.m_type && pending->second.m_bAdd)
                {
                    geometry.m_bAdd = true;
                    geometry.m_bVisible = pending->second.m_bVisible;
                    geometry.m_bHasMaterial = pending->second.m_bHasMaterial;
                    geometry.m_material = pending->second.m_material;
                }

                m_pendingGeometry[name] = std::move(geometry);
            }

            void O3DUI::queueAction(const string &name, function<void()> action)
            {
                if (m_bClosing.load())
                    return;

                lock_guard<mutex> lock(m_pendingMutex);
                if (!m_bClosing.load())
                    m_pendingActions[name] = std::move(action);
            }

            bool O3DUI::flushPending(void)
            {
                if (m_bClosing.load())
                    return false;

                unordered_map<string, PENDING_GEOMETRY> pendingGeometry;
                std::map<string, function<void()>> pendingActions;
                {
                    lock_guard<mutex> lock(m_pendingMutex);
                    pendingGeometry.swap(m_pendingGeometry);
                    pendingActions.swap(m_pendingActions);
                }

                for (auto &pending : pendingGeometry)
                    applyGeometry(pending.first, pending.second);

                for (auto &pending : pendingActions)
                {
                    if (m_bClosing.load())
                        break;
                    pending.second();
                }

                bool bChanged = !pendingGeometry.empty() || !pendingActions.empty();
                if (bChanged && !m_bClosing.load() && m_pScene)
                    m_pScene->ForceRedraw();

                return bChanged;
            }

            void O3DUI::applyGeometry(const string &name, PENDING_GEOMETRY &geometry)
            {
                if (!m_pScene || m_bClosing.load())
                    return;

                auto pScene = m_pScene->GetScene();
                bool bExists = pScene->HasGeometry(name);

                if (geometry.m_type == PENDING_GEOMETRY_TYPE::remove)
                {
                    if (bExists)
                        pScene->RemoveGeometry(name);
                    m_livePointClouds.erase(name);
                    m_liveMeshes.erase(name);
                    m_liveLineSets.erase(name);
                    m_liveMaterials.erase(name);
                    m_livePointCounts.erase(name);
                    return;
                }

                if (geometry.m_bHasMaterial)
                    m_liveMaterials[name] = geometry.m_material;
                auto material = m_liveMaterials.find(name);
                if (material == m_liveMaterials.end())
                    return;

                if (geometry.m_type == PENDING_GEOMETRY_TYPE::pointCloud)
                {
                    if (!geometry.m_pPointCloud || geometry.m_pPointCloud->IsEmpty())
                    {
                        if (bExists)
                            pScene->RemoveGeometry(name);
                        m_livePointClouds.erase(name);
                        m_livePointCounts.erase(name);
                        return;
                    }

                    size_t nPoints = (size_t)geometry.m_pPointCloud->GetPointPositions().GetLength();
                    bool bRecreate = geometry.m_bAdd || !bExists ||
                                     m_livePointCounts[name] != nPoints;
                    if (bRecreate)
                    {
                        if (bExists)
                            pScene->RemoveGeometry(name);
                        m_livePointClouds[name] = geometry.m_pPointCloud;
                        pScene->AddGeometry(name, m_livePointClouds[name].get(), material->second, false);
                        pScene->GetScene()->GeometryShadows(name, false, false);
                        pScene->ShowGeometry(name, geometry.m_bVisible);
                    }
                    else
                    {
                        pScene->GetScene()->UpdateGeometry(
                            name,
                            *geometry.m_pPointCloud,
                            rendering::Scene::kUpdatePointsFlag |
                                rendering::Scene::kUpdateColorsFlag);
                        m_livePointClouds[name] = geometry.m_pPointCloud;
                    }
                    m_livePointCounts[name] = nPoints;
                    return;
                }

                if (bExists)
                    pScene->RemoveGeometry(name);

                if (geometry.m_type == PENDING_GEOMETRY_TYPE::mesh)
                {
                    if (!geometry.m_pMesh || geometry.m_pMesh->IsEmpty())
                        return;
                    m_liveMeshes[name] = geometry.m_pMesh;
                    pScene->AddGeometry(name, m_liveMeshes[name].get(), material->second, false);
                }
                else if (geometry.m_type == PENDING_GEOMETRY_TYPE::lineSet)
                {
                    if (!geometry.m_pLineSet || geometry.m_pLineSet->IsEmpty())
                        return;
                    m_liveLineSets[name] = geometry.m_pLineSet;
                    pScene->AddGeometry(name, m_liveLineSets[name].get(), material->second);
                }

                pScene->GetScene()->GeometryShadows(name, false, false);
                pScene->ShowGeometry(name, geometry.m_bVisible);
            }

            void O3DUI::AddPointCloud(const string &name,
                                      t::geometry::PointCloud *pTpc,
                                      rendering::Material *pMaterial,
                                      bool bVisible)
            {
                NULL_(pTpc);
                NULL_(pMaterial);

                PENDING_GEOMETRY geometry;
                geometry.m_type = PENDING_GEOMETRY_TYPE::pointCloud;
                geometry.m_pPointCloud = make_shared<t::geometry::PointCloud>(pTpc->Clone());
                pMaterial->ToMaterialRecord(geometry.m_material);
                geometry.m_bHasMaterial = true;
                geometry.m_bAdd = true;
                geometry.m_bVisible = bVisible;
                queueGeometry(name, std::move(geometry));
            }

            void O3DUI::UpdatePointCloud(const string &name,
                                         t::geometry::PointCloud *pTpc)
            {
                NULL_(pTpc);

                PENDING_GEOMETRY geometry;
                geometry.m_type = PENDING_GEOMETRY_TYPE::pointCloud;
                geometry.m_pPointCloud = make_shared<t::geometry::PointCloud>(pTpc->Clone());
                queueGeometry(name, std::move(geometry));
            }

            void O3DUI::AddMesh(const string &name,
                                t::geometry::TriangleMesh *pTmesh,
                                rendering::Material *pMaterial,
                                bool bVisible)
            {
                NULL_(pTmesh);
                NULL_(pMaterial);

                PENDING_GEOMETRY geometry;
                geometry.m_type = PENDING_GEOMETRY_TYPE::mesh;
                geometry.m_pMesh = make_shared<t::geometry::TriangleMesh>(pTmesh->Clone());
                pMaterial->ToMaterialRecord(geometry.m_material);
                geometry.m_bHasMaterial = true;
                geometry.m_bAdd = true;
                geometry.m_bVisible = bVisible;
                queueGeometry(name, std::move(geometry));
            }

            void O3DUI::UpdateMesh(const string &name,
                                   t::geometry::TriangleMesh *pTmesh,
                                   rendering::Material *pMaterial)
            {
                NULL_(pTmesh);
                NULL_(pMaterial);

                PENDING_GEOMETRY geometry;
                geometry.m_type = PENDING_GEOMETRY_TYPE::mesh;
                geometry.m_pMesh = make_shared<t::geometry::TriangleMesh>(pTmesh->Clone());
                pMaterial->ToMaterialRecord(geometry.m_material);
                geometry.m_bHasMaterial = true;
                queueGeometry(name, std::move(geometry));
            }

            void O3DUI::AddLineSet(const string &name,
                                   geometry::LineSet *pLS,
                                   rendering::Material *pMaterial,
                                   bool bVisible)
            {
                NULL_(pLS);
                NULL_(pMaterial);

                PENDING_GEOMETRY geometry;
                geometry.m_type = PENDING_GEOMETRY_TYPE::lineSet;
                geometry.m_pLineSet = make_shared<geometry::LineSet>(*pLS);
                pMaterial->ToMaterialRecord(geometry.m_material);
                geometry.m_bHasMaterial = true;
                geometry.m_bAdd = true;
                geometry.m_bVisible = bVisible;
                queueGeometry(name, std::move(geometry));
            }

            void O3DUI::UpdateLineSet(const string &name,
                                      geometry::LineSet *pLS,
                                      rendering::Material *pMaterial)
            {
                NULL_(pLS);
                NULL_(pMaterial);

                PENDING_GEOMETRY geometry;
                geometry.m_type = PENDING_GEOMETRY_TYPE::lineSet;
                geometry.m_pLineSet = make_shared<geometry::LineSet>(*pLS);
                pMaterial->ToMaterialRecord(geometry.m_material);
                geometry.m_bHasMaterial = true;
                queueGeometry(name, std::move(geometry));
            }

            void O3DUI::RemoveGeometry(const string &name)
            {
                PENDING_GEOMETRY geometry;
                geometry.m_type = PENDING_GEOMETRY_TYPE::remove;
                queueGeometry(name, std::move(geometry));
            }

            void O3DUI::CamSetProj(
                double fov,
                double near,
                double far,
                uint8_t fov_type)
            {
                queueAction("1.camera.projection", [this, fov, near, far, fov_type]()
                            {
                                auto f = m_pScene->GetFrame();
                                auto sCam = m_pScene->GetScene()->GetCamera();
                                sCam->SetProjection(
                                    fov,
                                    float(f.width) / float(f.height),
                                    near,
                                    far,
                                    (fov_type == 0) ? Camera::FovType::Horizontal : Camera::FovType::Vertical);
                            });
            }

            void O3DUI::CamSetProj(
                Camera::Projection projType,
                double left,
                double right,
                double bottom,
                double top,
                double near,
                double far)
            {
                queueAction("1.camera.projection", [this, projType, left, right, bottom, top, near, far]()
                            {
                                auto sCam = m_pScene->GetScene()->GetCamera();
                                sCam->SetProjection(
                                    projType,
                                    left,
                                    right,
                                    bottom,
                                    top,
                                    near,
                                    far);
                            });
            }

            void O3DUI::CamSetPose(
                const Vector3f &center,
                const Vector3f &eye,
                const Vector3f &up)
            {
                queueAction("2.camera.pose", [this, center, eye, up]()
                            {
                                auto sCam = m_pScene->GetScene()->GetCamera();
                                sCam->LookAt(center, eye, up);
                            });
            }

            void O3DUI::CamAutoBound(const geometry::AxisAlignedBoundingBox &aabb,
                                     const Vector3f &CoR)
            {
                queueAction("0.camera.bound", [this, aabb, CoR]()
                            {
                                m_pScene->SetupCamera(
                                    m_pScene->GetScene()->GetCamera()->GetFieldOfView(),
                                    aabb,
                                    CoR);
                            });
            }

            void O3DUI::camMove(Vector3f vM)
            {
                queueAction("3.camera.move", [this, vM]() mutable
                            {
                                auto pC = m_pScene->GetScene()->GetCamera();
                                auto mm = pC->GetModelMatrix();
                                vM *= m_uiState.m_sMove;
                                mm.translate(vM);
                                pC->SetModelMatrix(mm);
                            });
            }

            UIState *O3DUI::getUIState(void)
            {
                return &m_uiState;
            }

            void O3DUI::UpdateUIstate(void)
            {
                m_pScene->EnableSceneCaching(m_uiState.m_bSceneCache);
                auto pO3DScene = m_pScene->GetScene();
                pO3DScene->ShowAxes(m_uiState.m_bShowAxes);
                pO3DScene->SetBackground(m_uiState.m_vBgCol, nullptr);
                pO3DScene->SetLighting(Open3DScene::LightingProfile::NO_SHADOWS, m_uiState.m_vSunDir);
                // SetPointSize(m_uiState.m_sPoint);
                // SetLineWidth(m_uiState.m_wLine);

                SetNeedsLayout();
                m_pScene->ForceRedraw();
            }

            void O3DUI::SetPointSize(const string &name, int px)
            {
                m_uiState.m_sPoint = px;
                px = int(ConvertToScaledPixels(px));
                // for (auto &o : m_vObject)
                // {
                //     o.m_material.SetPointSize(float(px));
                //     MaterialRecord mr;
                //     o.m_material.ToMaterialRecord(mr);
                //     m_pScene->GetScene()->GetScene()->OverrideMaterial(o.m_name, mr);
                // }
                m_pScene->SetPickablePointSize(px);

                m_pScene->ForceRedraw();
            }

            void O3DUI::SetLineWidth(const string &name, int px)
            {
                m_uiState.m_wLine = px;

                px = int(ConvertToScaledPixels(px));
                // for (auto &o : m_vObject)
                // {
                //     o.m_material.SetLineWidth(float(px));
                //     MaterialRecord mr;
                //     o.m_material.ToMaterialRecord(mr);
                //     m_pScene->GetScene()->GetScene()->OverrideMaterial(o.m_name, mr);
                // }
                m_pScene->ForceRedraw();
            }

            void O3DUI::ShowMsg(const char *pTitle, const char *pMsg, bool bOK)
            {
                auto em = GetTheme().font_size;
                auto margins = Margins(GetTheme().default_margin);
                auto dlg = std::make_shared<Dialog>(pTitle);
                auto layout = std::make_shared<Vert>(em, margins);
                layout->AddChild(std::make_shared<Label>(pMsg));
                if (bOK)
                {
                    auto ok = std::make_shared<Button>("OK");
                    ok->SetOnClicked([this]()
                                     { this->CloseDialog(); });
                    layout->AddChild(Horiz::MakeCentered(ok));
                }

                dlg->AddChild(layout);
                ShowDialog(dlg);
                PostRedraw();
            }

            void O3DUI::CloseMsg(void)
            {
                CloseDialog();
                PostRedraw();
            }

            void O3DUI::ExportCurrentImage(const string &path)
            {
                m_pScene->EnableSceneCaching(false);
                m_pScene->GetScene()->GetScene()->RenderToImage(
                    [this, path](shared_ptr<geometry::Image> image) mutable
                    {
                        if (!io::WriteImage(path, *image))
                        {
                            ShowMessageBox(
                                "Error",
                                (string("Could not write image to ") + path + ".").c_str());
                        }
                        m_pScene->EnableSceneCaching(m_uiState.m_bSceneCache);
                    });
            }

            string O3DUI::getBaseDirSave(void)
            {
                DIR *pDir = opendir(m_uiState.m_dirSave.c_str());
                if (!pDir)
                    return "";

                struct dirent *dir;
                string d = "";
                while ((dir = readdir(pDir)) != NULL)
                {
                    IF_CONT(dir->d_name[0] == '.');
                    IF_CONT(dir->d_type != 0x4); // 0x4: folder

                    d = m_uiState.m_dirSave + string(dir->d_name);
                    d = checkDirName(d);
                    break;
                }

                closedir(pDir);
                return d;
            }

            void O3DUI::Layout(const gui::LayoutContext &context)
            {
                m_pScene->SetFrame(GetContentRect());
                gui::Window::Layout(context);
            }

            float O3DUI::ConvertToScaledPixels(int px)
            {
                return round(px * GetScaling());
            }

        } // namespace visualizer
    } // namespace visualization
} // namespace open3d
