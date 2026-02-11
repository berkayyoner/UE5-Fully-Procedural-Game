/*************************************************************************
* ADOBE CONFIDENTIAL
* ___________________
*
*  Copyright 2015 Adobe Systems Incorporated
*  All Rights Reserved.
*
* NOTICE:  All information contained herein is, and remains
* the property of Adobe Systems Incorporated and its suppliers,
* if any.  The intellectual and technical concepts contained
* herein are proprietary to Adobe Systems Incorporated and its
* suppliers and are protected by all applicable intellectual property laws,
* including trade secret and or copyright laws.
* Dissemination of this information or reproduction of this material
* is strictly forbidden unless prior written permission is obtained
* from Adobe Systems Incorporated.
**************************************************************************/
import{viewerModuleUtils as o}from"../viewer-module-utils.js";import{dcLocalStorage as t}from"../../common/local-storage.js";import{util as e}from"../util.js";import{floodgate as n}from"../floodgate.js";import{loggingApi as a}from"../../common/loggingApi.js";import{removeExperimentCodeForAnalytics as r,setExperimentCodeForAnalytics as c}from"../../common/experimentUtils.js";const i=o=>{try{return JSON.parse(n.getFeatureMeta(o))}catch(t){return a.error({context:"Google Docs",message:`Failure in parsing FeatureFlag ${o}`,error:t.message||t.toString()}),{validPaths:["document","spreadsheets","presentation"],selectors:{touchPointContainer:["docs-titlebar-buttons"],docTitle:["docs-title-input"]}}}},s=({isEnLocaleEnabled:o,isNonEnLocaleEnabled:e})=>{const n="en-US"===t.getItem("locale")||"en-GB"===t.getItem("locale");return n&&o||!n&&e};async function l(t,a,l){await o.initializeViewerVariables(l);const g=!e.isAcrobatTouchPointEnabled("acrobat-touch-point-in-google-docs"),u=await n.hasFlag("dc-cv-google-docs-convert-to-pdf-touch-point"),d=await n.hasFlag("dc-cv-google-docs-convert-to-pdf-touch-point-control"),m=u&&i("dc-cv-google-docs-convert-to-pdf-touch-point"),T=d&&i("dc-cv-google-docs-convert-to-pdf-touch-point-control"),p=s(m)&&!g&&u,h=s(T)&&!g&&d;p?(c("GDCT"),r("GDCC")):h&&(c("GDCC"),r("GDCT"));const f=e.getTranslation("gmailConvertToPdf"),v=e.getTranslation("convertToPDFTouchPointTooltip"),F={enableGoogleDocsConvertToPDFTouchPoint:p,...m,text:{acrobatTouchPointTooltip:v,acrobatTouchPointText:f}};t?.surfaceNameTranslationKey&&(F.text.touchPointFTE={title:e.getTranslation("convertToPDFFTEHeading"),description:e.getTranslation("convertToPDFFTEBody",e.getTranslation(t?.surfaceNameTranslationKey)),button:e.getTranslation("closeButton")}),a(F)}export{l as googleDocsInit};